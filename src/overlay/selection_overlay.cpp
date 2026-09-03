#include "qingying/overlay/selection_overlay.hpp"

#include <Windows.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "qingying/app/app_messages.hpp"
#include "qingying/app/ui_message_channel.h"
#include "qingying/overlay/coordinate_transform.hpp"
#include "qingying/overlay/overlay_phase.hpp"
#include "qingying/overlay/overlay_renderer.hpp"
#include "qingying/overlay/selection_controller.hpp"
#include "qingying/overlay/selection_handles.hpp"
#include "qingying/overlay/selection_toolbar.hpp"
#include "qingying/window/window_detector.hpp"

namespace qingying {

namespace {

// 截图期间临时注册的全局 Esc 热键 id：遮罩不抢前台激活权（WS_EX_NOACTIVATE），
// 键盘消息不会发给遮罩，取消操作改由该热键投递 WM_HOTKEY 实现。
constexpr int kEscapeHotkeyId = 2;

// 覆盖层内部的拖拽类型：创建 / 调整大小 / 整体移动。
enum class DragKind { None, Create, Resize, Move };

// SelectionController 等 UI 状态通过窗口属性（WindowLongPtr）附加到窗口，
// 供 WndProc 在处理消息时访问，避免全局/静态可变量。
struct OverlayWindowData {
  SelectionController controller;
  SelectionCallback callback;
  SelectionClosedCallback closed_callback;
  std::atomic<std::uintptr_t>* owner_hwnd{nullptr};
  std::atomic<bool>* accepting_messages{nullptr};
  UiMessageChannel* message_channel{nullptr};
  bool window_destroyed{false};
  HWND overlay{nullptr};
  SelectionToolbar toolbar;
  SelectionAction action{SelectionAction::None};
  OverlayPhase phase{OverlayPhase::Sniffing};
  DragKind drag{DragKind::None};
  coord::VirtualScreenRect screen;
  // 按 DPI 缩放后的 UI 尺寸（100% 时等于基准常量）。
  int dpi{96};
  int handle_radius{handles::kHandleHitRadius};
  SelectionHandle active_handle{SelectionHandle::None};
  WindowDetector window_detector;  // 窗口吸附检测
  bool has_hover{false};           // 是否悬停在可吸附窗口上
  SelectionResult hover_rect;      // 悬停窗口矩形（客户区坐标）
  Image background;                // 遮罩界面背景（桌面截图，物理像素）；空则纯遮罩
  LongShotControlCallback longshot_control_callback;
  SelectionAction longshot_pending_action{SelectionAction::None};
  bool capture_passthrough{false};
  Image longshot_preview;
  Image annotated_image;
};

SelectionResult toScreenSelection(const SelectionResult& client,
                                  const coord::VirtualScreenRect& screen);
bool updateOverlay(HWND hwnd, OverlayWindowData* data);
bool showToolbar(HWND overlay, OverlayWindowData* data,
                 const SelectionResult& selection_screen,
                 const coord::VirtualScreenRect& screen);
void handleToolbarCommand(OverlayWindowData* data,
                          SelectionToolbarCommand command);

const wchar_t kOverlayClassName[] = L"QingYingSelectionOverlay";

void destroyToolbar(OverlayWindowData* data) {
  if (data == nullptr) {
    return;
  }
  data->toolbar.hide();
}

void refreshToolbar(OverlayWindowData* data) {
  if (data != nullptr) {
    data->toolbar.update(data->phase);
  }
}

void requestLongShotStop(OverlayWindowData* data) {
  if (data == nullptr || !overlayPhaseIsLongShot(data->phase) ||
      data->phase == OverlayPhase::LongShotFinishing) {
    return;
  }
  if (!transitionOverlayPhase(data->phase,
                              OverlayPhase::LongShotFinishing)) {
    return;
  }
  refreshToolbar(data);
  if (data->longshot_control_callback) {
    data->longshot_control_callback(LongShotControl::Stop);
  }
}

void chooseToolbarAction(OverlayWindowData* data, SelectionAction action) {
  if (data == nullptr || data->overlay == nullptr) {
    return;
  }
  if (action == SelectionAction::LongShot) {
    if (data->phase == OverlayPhase::LongShotRunning ||
        data->phase == OverlayPhase::LongShotPaused) {
      const OverlayPhase next =
          data->phase == OverlayPhase::LongShotRunning
              ? OverlayPhase::LongShotPaused
              : OverlayPhase::LongShotRunning;
      if (!transitionOverlayPhase(data->phase, next)) {
        return;
      }
      refreshToolbar(data);
      if (data->longshot_control_callback) {
        data->longshot_control_callback(LongShotControl::TogglePause);
      }
      updateOverlay(data->overlay, data);
      return;
    }

    if (!transitionOverlayPhase(data->phase,
                                OverlayPhase::LongShotRunning)) {
      return;
    }
    data->action = SelectionAction::LongShot;
    data->annotated_image = Image{};
    data->longshot_pending_action = SelectionAction::None;
    // During capture the selection hole must remain a real transparent hole;
    // otherwise the static desktop background would be captured repeatedly.
    data->capture_passthrough = true;
    refreshToolbar(data);
    updateOverlay(data->overlay, data);

    SelectionResult result = toScreenSelection(data->controller.selection(),
                                               data->screen);
    result.action = SelectionAction::LongShot;
    if (data->callback) {
      data->callback(result);
    }
    return;
  }
  if (overlayPhaseIsLongShot(data->phase)) {
    if (data->phase == OverlayPhase::LongShotPaused) {
      data->longshot_pending_action = action;
      requestLongShotStop(data);
    }
    return;
  }
  data->action = action;
  PostMessageW(data->overlay, WM_CLOSE, 0, 0);
}

void handleToolbarCommand(OverlayWindowData* data,
                          SelectionToolbarCommand command) {
  if (data == nullptr) {
    return;
  }
  switch (command) {
    case SelectionToolbarCommand::Copy:
      chooseToolbarAction(data, SelectionAction::Copy);
      break;
    case SelectionToolbarCommand::Save:
      chooseToolbarAction(data, SelectionAction::Save);
      break;
    case SelectionToolbarCommand::ToggleLongShot:
      chooseToolbarAction(data, SelectionAction::LongShot);
      break;
    case SelectionToolbarCommand::Edit:
      chooseToolbarAction(data, SelectionAction::Edit);
      break;
    case SelectionToolbarCommand::Pin:
      chooseToolbarAction(data, SelectionAction::Pin);
      break;
    case SelectionToolbarCommand::StopLongShot:
      requestLongShotStop(data);
      break;
    case SelectionToolbarCommand::Cancel:
      data->controller.cancel();
      data->action = SelectionAction::None;
      PostMessageW(data->overlay, WM_CLOSE, 0, 0);
      break;
  }
}

// Win32 系统光标资源：使用显式资源编号，避免项目未定义 UNICODE 时
// IDC_* 宏与 LoadCursorW 的字符类型不匹配。
HCURSOR loadOverlayCursor(SelectionHandle handle, bool has_selection) {
  int resource_id = 32512;  // IDC_ARROW
  if (!has_selection) {
    resource_id = 32515;  // IDC_CROSS
  } else {
    switch (handle) {
      case SelectionHandle::Top:
      case SelectionHandle::Bottom:
        resource_id = 32645;  // IDC_SIZENS
        break;
      case SelectionHandle::Left:
      case SelectionHandle::Right:
        resource_id = 32644;  // IDC_SIZEWE
        break;
      case SelectionHandle::TopLeft:
      case SelectionHandle::BottomRight:
        resource_id = 32642;  // IDC_SIZENWSE
        break;
      case SelectionHandle::TopRight:
      case SelectionHandle::BottomLeft:
        resource_id = 32643;  // IDC_SIZENESW
        break;
      case SelectionHandle::Move:
        resource_id = 32646;  // IDC_SIZEALL
        break;
      case SelectionHandle::None:
        break;
    }
  }
  return LoadCursorW(nullptr, MAKEINTRESOURCEW(resource_id));
}

void updateOverlayCursor(OverlayWindowData* data, int x, int y) {
  if (data == nullptr) {
    return;
  }

  const bool selection_editable =
      overlayPhaseHasSelection(data->phase) && data->annotated_image.empty();
  SelectionHandle handle = SelectionHandle::None;
  if (data->drag == DragKind::Resize || data->drag == DragKind::Move) {
    handle = data->active_handle;
  } else if (selection_editable) {
    handle = data->controller.hitTest(x, y);
  }

  HCURSOR cursor = loadOverlayCursor(
      handle, overlayPhaseHasSelection(data->phase));
  if (cursor != nullptr) {
    SetCursor(cursor);
  }
}

// 选区从覆盖层客户区坐标 → 屏幕坐标（供工具栏定位与最终出参使用）。
SelectionResult toScreenSelection(const SelectionResult& client,
                                  const coord::VirtualScreenRect& screen) {
  SelectionResult out = client;
  out.x += screen.left;
  out.y += screen.top;
  return out;
}

// 窗口吸附悬停检测：把鼠标客户区坐标转成屏幕坐标交给 WindowDetector，
// 找到候选窗口后再转回客户区坐标，保存为悬停矩形。
void updateHover(OverlayWindowData* data, int client_x, int client_y) {
  if (data == nullptr) {
    return;
  }
  const int screen_x = coord::clientToScreenX(client_x, data->screen);
  const int screen_y = coord::clientToScreenY(client_y, data->screen);

  HWND window = nullptr;
  WindowRect rect;
  if (data->window_detector.detectAt(screen_x, screen_y, window, rect)) {
    data->hover_rect.cancelled = false;
    data->hover_rect.x = coord::screenToClientX(rect.left, data->screen);
    data->hover_rect.y = coord::screenToClientY(rect.top, data->screen);
    data->hover_rect.width = rect.width();
    data->hover_rect.height = rect.height();
    data->has_hover = true;
  } else {
    data->has_hover = false;
  }
}

bool showToolbar(HWND overlay, OverlayWindowData* data,
                 const SelectionResult& selection_screen,
                 const coord::VirtualScreenRect& screen) {
  if (data == nullptr) {
    return false;
  }
  SelectionToolbarPlacement placement;
  placement.selection_x = selection_screen.x;
  placement.selection_y = selection_screen.y;
  placement.selection_width = selection_screen.width;
  placement.selection_height = selection_screen.height;
  placement.screen_left = screen.left;
  placement.screen_top = screen.top;
  placement.screen_right = screen.right();
  placement.screen_bottom = screen.bottom();
  return data->toolbar.show(
      overlay, placement, data->phase,
      [data](SelectionToolbarCommand command) {
        handleToolbarCommand(data, command);
      });
}

// Re-render the layered-window frame from the current interaction state.
bool updateOverlay(HWND hwnd, OverlayWindowData* data) {
  if (data == nullptr) {
    return false;
  }
  const SelectionResult& selection = data->controller.selection();
  const OverlayRenderState render_state(
      selection, data->hover_rect, data->background, data->longshot_preview,
      data->phase,
      overlayPhaseHasSelection(data->phase) && !selection.cancelled &&
          data->annotated_image.empty(),
      data->handle_radius, data->drag == DragKind::None && data->has_hover,
      data->capture_passthrough);
  return OverlayRenderer::render(hwnd, data->screen, render_state);
}

LRESULT CALLBACK overlayWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                LPARAM lparam) {
  OverlayWindowData* data = reinterpret_cast<OverlayWindowData*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (msg) {
    case WM_NCCREATE: {
      const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
      OverlayWindowData* create_data =
          static_cast<OverlayWindowData*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(create_data));
      if (create_data != nullptr) {
        create_data->overlay = hwnd;
      }
      return TRUE;
    }
    case WM_QINGYING_SELECTION_OVERLAY_READY: {
      if (data == nullptr) {
        return 0;
      }
      if (!updateOverlay(hwnd, data)) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
      }
      if (data->phase == OverlayPhase::Selected) {
        const SelectionResult selection_screen =
            toScreenSelection(data->controller.selection(), data->screen);
        if (!showToolbar(hwnd, data, selection_screen, data->screen)) {
          data->controller.cancel();
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
      }
      return 0;
    }
    case WM_QINGYING_SELECTION_LONGSHOT_PREVIEW: {
      if (data != nullptr && data->message_channel != nullptr) {
        const auto message =
            data->message_channel->take<SelectionOverlayLongShotPreviewMessage>(
                static_cast<UiMessageToken>(lparam));
        if (message.has_value()) {
          data->longshot_preview = std::move(message->image);
          updateOverlay(hwnd, data);
        }
      }
      return 0;
    }
    case WM_QINGYING_SELECTION_LONGSHOT_FINISHED: {
      std::optional<SelectionOverlayLongShotFinishedMessage> message;
      if (data != nullptr && data->message_channel != nullptr) {
        message = data->message_channel->take<
            SelectionOverlayLongShotFinishedMessage>(
            static_cast<UiMessageToken>(lparam));
      }
      if (data != nullptr) {
        const bool success = message.has_value() && message->success;
        const SelectionAction pending_action = data->longshot_pending_action;
        const bool run_pending_action =
            success && pending_action != SelectionAction::None;
        if (!transitionOverlayPhase(data->phase, OverlayPhase::Selected)) {
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
          return 0;
        }
        data->capture_passthrough = false;
        data->longshot_pending_action = SelectionAction::None;
        if (!success) {
          // Failure dialogs are shown by Application only after this topmost
          // fullscreen window has gone away.
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
          return 0;
        }
        if (run_pending_action) {
          data->action = pending_action;
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
          return 0;
        }
        refreshToolbar(data);
        updateOverlay(hwnd, data);
      }
      return 0;
    }
    case WM_QINGYING_SELECTION_OVERLAY_ABORT: {
      if (data != nullptr) {
        data->callback = {};
        data->closed_callback = {};
        data->longshot_control_callback = {};
        data->controller.cancel();
        data->action = SelectionAction::None;
      }
      if (data != nullptr) {
        (void)transitionOverlayPhase(data->phase, OverlayPhase::Closing);
      }
      DestroyWindow(hwnd);
      return 0;
    }
    case WM_SETCURSOR: {
      if (data == nullptr) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
      }
      POINT cursor_point{};
      GetCursorPos(&cursor_point);
      ScreenToClient(hwnd, &cursor_point);
      updateOverlayCursor(data, cursor_point.x, cursor_point.y);
      return TRUE;
    }
    case WM_LBUTTONDOWN: {
      if (data == nullptr) {
        return 0;
      }
      if (overlayPhaseIsLongShot(data->phase)) {
        return 0;
      }
      // 标注后的栅格结果与当前物理选区是一组不可拆分的结果。恢复操作条
      // 时锁定选区，避免拖动 / 缩放后继续对尺寸不匹配的旧图执行动作。
      if (!data->annotated_image.empty()) {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));

      if (!overlayPhaseHasSelection(data->phase)) {
        // 尚无有效选区：开始拖出矩形。
        if (!transitionOverlayPhase(data->phase, OverlayPhase::Creating)) {
          return 0;
        }
        SetCapture(hwnd);
        data->drag = DragKind::Create;
        data->active_handle = SelectionHandle::None;
        data->controller.begin(x, y);
        return 0;
      }

      // 已有有效选区：清除悬停，命中测试决定「调整 / 移动 / 重新框选」。
      data->has_hover = false;
      const SelectionHandle handle = data->controller.hitTest(x, y);
      if (handle == SelectionHandle::None) {
        destroyToolbar(data);
        if (!transitionOverlayPhase(data->phase, OverlayPhase::Creating)) {
          return 0;
        }
        data->action = SelectionAction::None;
        SetCapture(hwnd);
        data->drag = DragKind::Create;
        data->active_handle = SelectionHandle::None;
        data->controller.begin(x, y);
      } else if (handle == SelectionHandle::Move) {
        destroyToolbar(data);
        SetCapture(hwnd);
        data->drag = DragKind::Move;
        data->active_handle = SelectionHandle::Move;
        data->controller.beginMove(x, y);
      } else {
        destroyToolbar(data);
        SetCapture(hwnd);
        data->drag = DragKind::Resize;
        data->active_handle = handle;
        data->controller.beginResize(handle, x, y);
      }
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (data == nullptr) {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      updateOverlayCursor(data, x, y);

      if (data->drag == DragKind::None) {
        // 空闲悬停：无按键且未确认选区 → 窗口吸附高亮。
        if ((wparam & MK_LBUTTON) == 0 &&
            data->phase == OverlayPhase::Sniffing) {
          updateHover(data, x, y);
          updateOverlay(hwnd, data);
        }
        return 0;
      }

      if ((wparam & MK_LBUTTON) == 0) {
        return 0;
      }
      switch (data->drag) {
        case DragKind::Create:
          data->controller.update(x, y);
          break;
        case DragKind::Resize:
          data->controller.updateResize(x, y);
          break;
        case DragKind::Move:
          data->controller.updateMove(x, y);
          break;
        default:
          break;
      }
      updateOverlay(hwnd, data);
      return 0;
    }
    case WM_LBUTTONUP: {
      if (data == nullptr || data->drag == DragKind::None) {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      ReleaseCapture();
      switch (data->drag) {
        case DragKind::Create:
          data->controller.update(x, y);
          data->controller.confirm();
          if (data->controller.selection().cancelled && data->has_hover) {
            // 纯点击未拖拽 → 吸附悬停窗口。
            data->controller.setSelection(
                data->hover_rect.x, data->hover_rect.y, data->hover_rect.width,
                data->hover_rect.height);
          }
          break;
        case DragKind::Resize:
          data->controller.updateResize(x, y);
          data->controller.endDrag();
          break;
        case DragKind::Move:
          data->controller.updateMove(x, y);
          data->controller.endDrag();
          break;
        default:
          break;
      }
      data->drag = DragKind::None;
      data->active_handle = SelectionHandle::None;

      const SelectionResult& selection = data->controller.selection();
      if (selection.cancelled) {
        (void)transitionOverlayPhase(data->phase, OverlayPhase::Sniffing);
        destroyToolbar(data);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
      }

      if (!transitionOverlayPhase(data->phase, OverlayPhase::Selected)) {
        data->controller.cancel();
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
      }
      const SelectionResult selection_screen =
          toScreenSelection(selection, data->screen);
      if (!showToolbar(hwnd, data, selection_screen, data->screen)) {
        data->controller.cancel();
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
      }
      updateOverlay(hwnd, data);
      return 0;
    }
    case WM_RBUTTONDOWN: {
      if (data != nullptr) {
        if (overlayPhaseIsLongShot(data->phase)) {
          requestLongShotStop(data);
          return 0;
        }
        data->controller.cancel();
        data->action = SelectionAction::None;
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      }
      return 0;
    }
    case WM_HOTKEY: {
      // 截图期间临时注册的全局 Esc 热键：遮罩不激活（WS_EX_NOACTIVATE），
      // 键盘消息不会发给遮罩，取消改由这里处理。
      if (data != nullptr && wparam == kEscapeHotkeyId) {
        if (overlayPhaseIsLongShot(data->phase)) {
          requestLongShotStop(data);
          return 0;
        }
        data->controller.cancel();
        data->action = SelectionAction::None;
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      }
      return 0;
    }
    case WM_KEYDOWN: {
      if (data != nullptr && wparam == VK_ESCAPE) {
        if (overlayPhaseIsLongShot(data->phase)) {
          requestLongShotStop(data);
          return 0;
        }
        data->controller.cancel();
        data->action = SelectionAction::None;
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      }
      return 0;
    }
    case WM_CLOSE: {
      if (data != nullptr) {
        (void)transitionOverlayPhase(data->phase, OverlayPhase::Closing);
      }
      destroyToolbar(data);
      DestroyWindow(hwnd);
      return 0;
    }
    case WM_DESTROY: {
      if (data != nullptr) {
        if (data->phase != OverlayPhase::Closing) {
          (void)transitionOverlayPhase(data->phase, OverlayPhase::Closing);
        }
        destroyToolbar(data);
        SelectionResult result = toScreenSelection(data->controller.selection(),
                                                   data->screen);
        result.action = data->action;
        result.annotated_image = std::move(data->annotated_image);
        SelectionCallback callback = std::move(data->callback);
        SelectionClosedCallback closed_callback =
            std::move(data->closed_callback);
        if (data->owner_hwnd != nullptr) {
          data->owner_hwnd->store(0);
        }
        if (data->accepting_messages != nullptr) {
          data->accepting_messages->store(false);
        }
        if (data->message_channel != nullptr) {
          data->message_channel->drain();
        }
        UnregisterHotKey(hwnd, kEscapeHotkeyId);
        if (callback && result.action != SelectionAction::LongShot) {
          callback(result);
        }
        if (closed_callback) {
          closed_callback();
        }
      }
      return 0;
    }
    case WM_NCDESTROY: {
      if (data != nullptr) {
        data->window_destroyed = true;
        if (data->accepting_messages != nullptr) {
          data->accepting_messages->store(false);
        }
        if (data->message_channel != nullptr) {
          data->message_channel->drain();
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      }
      return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

struct SelectionOverlay::Impl {
  std::unique_ptr<OverlayWindowData> window_data;
  UiMessageChannel messages;
  std::atomic<bool> accepting_messages{false};
};

SelectionOverlay::SelectionOverlay() : impl_(std::make_unique<Impl>()) {}

SelectionOverlay::~SelectionOverlay() {
  hide();
  drainMessages();
}

bool SelectionOverlay::show(const Image& background,
                             SelectionCallback callback,
                             LongShotControlCallback
                                 longshot_control_callback,
                             const SelectionResult& initial_selection,
                             SelectionClosedCallback closed_callback) {
  if (isVisible()) {
    return false;
  }

  if (impl_->window_data != nullptr) {
    if (!impl_->window_data->window_destroyed) {
      return false;
    }
    impl_->window_data.reset();
  }
  impl_->messages.drain();

  HINSTANCE instance = GetModuleHandleW(nullptr);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.lpfnWndProc = overlayWndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32515));  // IDC_CROSS
  wc.lpszClassName = kOverlayClassName;
  if (RegisterClassExW(&wc) == 0 &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  auto data = std::make_unique<OverlayWindowData>();
  data->message_channel = &impl_->messages;
  data->accepting_messages = &impl_->accepting_messages;
  data->background = background;  // 桌面截图背景（物理像素）；空则纯遮罩
  data->callback = std::move(callback);
  data->closed_callback = std::move(closed_callback);
  data->longshot_control_callback = std::move(longshot_control_callback);
  data->owner_hwnd = &overlay_hwnd_;
  data->screen = coord::getVirtualScreen();
  data->controller.setBounds(data->screen.width, data->screen.height);

  if (!initial_selection.cancelled && initial_selection.width > 0 &&
      initial_selection.height > 0) {
    data->controller.setSelection(
        coord::screenToClientX(initial_selection.x, data->screen),
        coord::screenToClientY(initial_selection.y, data->screen),
        initial_selection.width, initial_selection.height);
    if (!data->controller.selection().cancelled) {
      (void)transitionOverlayPhase(data->phase, OverlayPhase::Selected);
      data->annotated_image = initial_selection.annotated_image;
    }
  }

  // 按 DPI 缩放 UI（100% 为 96 DPI）：手柄命中半径。
  data->dpi = coord::getSystemDpi();
  const auto scale = [&](int value) { return MulDiv(value, data->dpi, 96); };
  data->handle_radius = scale(handles::kHandleHitRadius);
  data->controller.setHandleRadius(data->handle_radius);

  HWND hwnd = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
      kOverlayClassName, L"", WS_POPUP | WS_VISIBLE, data->screen.left,
      data->screen.top, data->screen.width, data->screen.height, nullptr,
      nullptr, instance, data.get());
  if (hwnd == nullptr) {
    impl_->messages.drain();
    return false;
  }
  impl_->window_data = std::move(data);
  overlay_hwnd_.store(reinterpret_cast<std::uintptr_t>(hwnd));
  impl_->accepting_messages.store(true);

  // 遮罩不抢前台激活权：WS_EX_NOACTIVATE 保证点击/显示都不会激活遮罩，
  // 原前台窗口保持激活，其从属浮层（owned popup）不会因失活而隐藏。
  // 键盘取消改由临时全局 Esc 热键提供，窗口销毁时注销。
  (void)RegisterHotKey(hwnd, kEscapeHotkeyId, 0, VK_ESCAPE);

  // 首帧渲染（UpdateLayeredWindow 需要窗口可见）。
  PostMessageW(hwnd, WM_QINGYING_SELECTION_OVERLAY_READY, 0, 0);
  return true;
}

bool SelectionOverlay::postLongShotPreview(const Image& image) {
  const HWND hwnd = reinterpret_cast<HWND>(overlay_hwnd_.load());
  if (hwnd == nullptr || !impl_->accepting_messages.load()) {
    return false;
  }
  try {
    SelectionOverlayLongShotPreviewMessage message;
    message.image = OverlayRenderer::makeLongShotPreviewImage(image);
    const auto token = impl_->messages.push(std::move(message));
    if (!token.has_value()) {
      return false;
    }
    if (!PostMessageW(hwnd, WM_QINGYING_SELECTION_LONGSHOT_PREVIEW, 0,
                      static_cast<LPARAM>(*token))) {
      impl_->messages.discard(*token);
      return false;
    }
  } catch (...) {
    return false;
  }
  return true;
}

bool SelectionOverlay::postLongShotFinished(bool success) {
  const HWND hwnd = reinterpret_cast<HWND>(overlay_hwnd_.load());
  if (hwnd == nullptr || !impl_->accepting_messages.load()) {
    return false;
  }
  SelectionOverlayLongShotFinishedMessage message;
  message.success = success;
  const auto token = impl_->messages.push(std::move(message));
  if (!token.has_value()) {
    return false;
  }
  if (!PostMessageW(hwnd, WM_QINGYING_SELECTION_LONGSHOT_FINISHED, 0,
                    static_cast<LPARAM>(*token))) {
    impl_->messages.discard(*token);
    return false;
  }
  return true;
}

void SelectionOverlay::hide() {
  impl_->accepting_messages.store(false);
  const HWND hwnd = reinterpret_cast<HWND>(overlay_hwnd_.load());
  if (hwnd != nullptr && IsWindow(hwnd)) {
    const DWORD window_thread = GetWindowThreadProcessId(hwnd, nullptr);
    if (window_thread == GetCurrentThreadId()) {
      SendMessageW(hwnd, WM_QINGYING_SELECTION_OVERLAY_ABORT, 0, 0);
    } else {
      PostMessageW(hwnd, WM_QINGYING_SELECTION_OVERLAY_ABORT, 0, 0);
    }
  }
  impl_->messages.drain();
}

void SelectionOverlay::drainMessages() noexcept {
  impl_->messages.drain();
}

bool SelectionOverlay::isVisible() const noexcept {
  const HWND hwnd = reinterpret_cast<HWND>(overlay_hwnd_.load());
  return hwnd != nullptr && IsWindow(hwnd) != FALSE;
}

}  // namespace qingying
