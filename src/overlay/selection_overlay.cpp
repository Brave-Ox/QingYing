#include "qingying/overlay/selection_overlay.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>
#include <vector>

#include "qingying/overlay/coordinate_transform.hpp"
#include "qingying/overlay/mask_renderer.hpp"
#include "qingying/overlay/overlay_phase.hpp"
#include "qingying/overlay/selection_controller.hpp"
#include "qingying/overlay/selection_handles.hpp"
#include "qingying/overlay/selection_toolbar.hpp"
#include "qingying/window/window_detector.hpp"

namespace qingying {

namespace {

// 遮罩窗口的自定义消息：通知窗口属性已就绪，可开始渲染首帧。
constexpr UINT kMsgOverlayReady = WM_APP + 1;
constexpr UINT kMsgLongShotPreview = WM_APP + 3;
constexpr UINT kMsgLongShotFinished = WM_APP + 4;
constexpr UINT kMsgOverlayAbort = WM_APP + 5;
// 截图期间临时注册的全局 Esc 热键 id：遮罩不抢前台激活权（WS_EX_NOACTIVATE），
// 键盘消息不会发给遮罩，取消操作改由该热键投递 WM_HOTKEY 实现。
constexpr int kEscapeHotkeyId = 2;
constexpr std::uint32_t kHandlePixel = 0xFFFFFFFFu;  // 手柄：不透明白
constexpr std::uint32_t kHoverPixel = 0xFF00B4FFu;   // 窗口吸附悬停高亮：亮蓝
constexpr int kHoverThickness = 3;

// 覆盖层内部的拖拽类型：创建 / 调整大小 / 整体移动。
enum class DragKind { None, Create, Resize, Move };

// SelectionController 等 UI 状态通过窗口属性（WindowLongPtr）附加到窗口，
// 供 WndProc 在处理消息时访问，避免全局/静态可变量。
struct OverlayWindowData {
  SelectionController controller;
  SelectionCallback callback;
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

struct LongShotPreviewMessage {
  Image image;
};

struct LongShotFinishedMessage {
  bool success{false};
};

SelectionResult toScreenSelection(const SelectionResult& client,
                                  const coord::VirtualScreenRect& screen);
bool updateOverlay(HWND hwnd, OverlayWindowData* data);
bool showToolbar(HWND overlay, OverlayWindowData* data,
                 const SelectionResult& selection_screen,
                 const coord::VirtualScreenRect& screen);
void handleToolbarCommand(OverlayWindowData* data,
                          SelectionToolbarCommand command);

// CreateCompatibleDC RAII：DeleteDC。
struct CompatibleDcDeleter {
  void operator()(HDC hdc) const noexcept {
    if (hdc != nullptr) {
      DeleteDC(hdc);
    }
  }
};

// CreateDIBSection 的 HBITMAP RAII：DeleteObject。
struct DibDeleter {
  void operator()(HBITMAP bitmap) const noexcept {
    if (bitmap != nullptr) {
      DeleteObject(bitmap);
    }
  }
};

// GetDC 的 HDC RAII：ReleaseDC。
struct ScreenHdcDeleter {
  void operator()(HDC hdc) const noexcept {
    if (hdc != nullptr) {
      ReleaseDC(nullptr, hdc);
    }
  }
};

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
      reinterpret_cast<std::uintptr_t>(overlay), placement, data->phase,
      [data](SelectionToolbarCommand command) {
        handleToolbarCommand(data, command);
      });
}

// 像素缓冲写点（越界跳过）。
void setOverlayPixel(std::vector<std::uint32_t>& pixels, int width, int height,
                     int x, int y, std::uint32_t color) {
  if (x < 0 || y < 0 || x >= width || y >= height) {
    return;
  }
  pixels.at(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
            static_cast<std::size_t>(x)) = color;
}

// 绘制窗口吸附悬停高亮边框（矩形轮廓）。
void drawHoverOutline(std::vector<std::uint32_t>& pixels, int width, int height,
                      const SelectionResult& rect, std::uint32_t color,
                      int thickness) {
  if (rect.width <= 0 || rect.height <= 0) {
    return;
  }
  const int right = rect.x + rect.width - 1;
  const int bottom = rect.y + rect.height - 1;
  for (int t = 0; t < thickness; ++t) {
    for (int i = rect.x; i <= right; ++i) {
      setOverlayPixel(pixels, width, height, i, rect.y + t, color);
      setOverlayPixel(pixels, width, height, i, bottom - t, color);
    }
    for (int j = rect.y; j <= bottom; ++j) {
      setOverlayPixel(pixels, width, height, rect.x + t, j, color);
      setOverlayPixel(pixels, width, height, right - t, j, color);
    }
  }
}

bool rectanglesIntersect(int left_a, int top_a, int right_a, int bottom_a,
                         int left_b, int top_b, int right_b, int bottom_b) {
  return left_a < right_b && left_b < right_a && top_a < bottom_b &&
         top_b < bottom_a;
}

void drawLongShotPreview(std::vector<std::uint32_t>& pixels, int width,
                         int height, const SelectionResult& selection,
                         const Image& preview) {
  if (preview.empty() || width <= 0 || height <= 0) {
    return;
  }

  constexpr int kMaxPreviewWidth = 440;
  constexpr int kMaxPreviewHeight = 720;
  constexpr int kPreviewPadding = 6;
  constexpr int kPreviewGap = 14;
  constexpr std::uint32_t kPanelPixel = 0xF0222222u;
  constexpr std::uint32_t kPanelBorder = 0xFFFF8000u;

  const int scale_x = kMaxPreviewWidth / preview.width;
  const int scale_y = kMaxPreviewHeight / preview.height;
  const int scale = (std::max)(1, (std::min)(scale_x, scale_y));
  const int image_width =
      (std::max)(1, (std::min)(kMaxPreviewWidth, preview.width * scale));
  const int image_height =
      (std::max)(1, (std::min)(kMaxPreviewHeight, preview.height * scale));
  const int panel_width = image_width + kPreviewPadding * 2;
  const int panel_height = image_height + kPreviewPadding * 2;

  const int selection_left = selection.x;
  const int selection_top = selection.y;
  const int selection_right = selection.x + selection.width;
  const int selection_bottom = selection.y + selection.height;

  struct Candidate {
    int x;
    int y;
  };
  const Candidate candidates[] = {
      {selection_right + kPreviewGap, selection_top},
      {selection_left - panel_width - kPreviewGap, selection_top},
      {selection_left, selection_bottom + kPreviewGap},
      {selection_left, selection_top - panel_height - kPreviewGap},
      {(width - panel_width) / 2, (height - panel_height) / 2},
  };

  int panel_x = candidates[0].x;
  int panel_y = candidates[0].y;
  for (const Candidate& candidate : candidates) {
    const int right = candidate.x + panel_width;
    const int bottom = candidate.y + panel_height;
    if (candidate.x >= 0 && candidate.y >= 0 && right <= width &&
        bottom <= height &&
        !rectanglesIntersect(candidate.x, candidate.y, right, bottom,
                             selection_left, selection_top, selection_right,
                             selection_bottom)) {
      panel_x = candidate.x;
      panel_y = candidate.y;
      break;
    }
  }

  panel_x = (std::max)(0, (std::min)(panel_x, width - panel_width));
  panel_y = (std::max)(0, (std::min)(panel_y, height - panel_height));

  for (int y = 0; y < panel_height; ++y) {
    for (int x = 0; x < panel_width; ++x) {
      const bool border = x == 0 || y == 0 || x == panel_width - 1 ||
                          y == panel_height - 1;
      setOverlayPixel(pixels, width, height, panel_x + x, panel_y + y,
                      border ? kPanelBorder : kPanelPixel);
    }
  }

  for (int y = 0; y < image_height; ++y) {
    const int source_y = (std::min)(
        preview.height - 1, static_cast<int>(
                                 static_cast<std::int64_t>(y) * preview.height /
                                 image_height));
    for (int x = 0; x < image_width; ++x) {
      const int source_x = (std::min)(
          preview.width - 1, static_cast<int>(
                                 static_cast<std::int64_t>(x) * preview.width /
                                 image_width));
      setOverlayPixel(
          pixels, width, height, panel_x + kPreviewPadding + x,
          panel_y + kPreviewPadding + y,
          preview.pixels[static_cast<std::size_t>(source_y) *
                             static_cast<std::size_t>(preview.width) +
                         static_cast<std::size_t>(source_x)]);
    }
  }
}

// 重新渲染遮罩到分层窗口。返回 false 表示渲染失败。
bool updateOverlay(HWND hwnd, OverlayWindowData* data) {
  const coord::VirtualScreenRect& screen = data->screen;
  const SelectionResult& selection = data->controller.selection();
  const int width = screen.width;
  const int height = screen.height;
  if (width <= 0 || height <= 0) {
    return false;
  }

  std::unique_ptr<HDC__, ScreenHdcDeleter> screen_dc(GetDC(nullptr));
  if (screen_dc == nullptr) {
    return false;
  }
  std::unique_ptr<HDC__, CompatibleDcDeleter> mem_dc(
      CreateCompatibleDC(screen_dc.get()));
  if (mem_dc == nullptr) {
    return false;
  }

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width;
  bmi.bmiHeader.biHeight = -height;  // 顶向下
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void* dib_bits = nullptr;
  std::unique_ptr<HBITMAP__, DibDeleter> dib(CreateDIBSection(
      mem_dc.get(), &bmi, DIB_RGB_COLORS, &dib_bits, nullptr, 0));
  if (dib == nullptr || dib_bits == nullptr) {
    return false;
  }

  std::vector<std::uint32_t> pixels;
  mask::renderFullscreenMask(width, height, selection, pixels);
  const std::size_t num_pixels =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (pixels.size() != num_pixels) {
    return false;
  }
  if (overlayPhaseHasSelection(data->phase) && !selection.cancelled &&
      data->annotated_image.empty()) {
    handles::drawHandles(pixels, width, height, selection.x, selection.y,
                         selection.width, selection.height, kHandlePixel,
                         data->handle_radius);
  } else if (data->drag == DragKind::None && data->has_hover) {
    drawHoverOutline(pixels, width, height, data->hover_rect, kHoverPixel,
                     kHoverThickness);
  }
  // Draw before the capture passthrough clear so an impossible placement
  // (for example, a selection covering almost the whole screen) is clipped
  // out of the selected pixels and can never contaminate a captured frame.
  drawLongShotPreview(pixels, width, height, selection, data->longshot_preview);

  if (data->capture_passthrough && overlayPhaseHasSelection(data->phase) &&
      !selection.cancelled) {
    // Remove the border/handles as well as the hole's 1-alpha marker while a
    // worker captures. This keeps the selected pixels free of overlay UI.
    const int left = (std::max)(0, selection.x);
    const int top = (std::max)(0, selection.y);
    const int right = (std::min)(width, selection.x + selection.width);
    const int bottom = (std::min)(height, selection.y + selection.height);
    for (int y = top; y < bottom; ++y) {
      for (int x = left; x < right; ++x) {
        setOverlayPixel(pixels, width, height, x, y, 0x00000000u);
      }
    }
  }

  // 有桌面背景时：遮罩界面 = 背景截图 + 遮罩合成。其它窗口（含从属浮层）
  // 被包含在截图里，在遮罩界面中保持可见，不再被实时 topmost 窗口物理盖住。
  // 无背景（截屏失败）时退回纯遮罩，保留原 premultiplied alpha 行为。
  if (!data->capture_passthrough && !data->background.empty()) {
    std::vector<std::uint32_t> composed;
    if (!mask::composeBackground(data->background, pixels, composed)) {
      return false;
    }
    pixels.swap(composed);
  }
  // DIB 与像素缓冲同布局（BGRA，顶向下），直接拷贝。
  std::copy(pixels.begin(), pixels.end(),
            reinterpret_cast<std::uint32_t*>(dib_bits));

  POINT dst{screen.left, screen.top};
  SIZE size{width, height};
  POINT pt_src{0, 0};
  BLENDFUNCTION blend{};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = 255;
  blend.AlphaFormat = AC_SRC_ALPHA;  // per-pixel alpha（premultiplied）

  const HGDIOBJ old_bitmap = SelectObject(mem_dc.get(), dib.get());
  if (old_bitmap == nullptr || old_bitmap == HGDI_ERROR) {
    return false;
  }
  const BOOL ok = UpdateLayeredWindow(hwnd, screen_dc.get(), &dst, &size,
                                      mem_dc.get(), &pt_src, 0, &blend,
                                      ULW_ALPHA);
  SelectObject(mem_dc.get(), old_bitmap);
  return ok != FALSE;
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
    case kMsgOverlayReady: {
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
    case kMsgLongShotPreview: {
      std::unique_ptr<LongShotPreviewMessage> message(
          reinterpret_cast<LongShotPreviewMessage*>(wparam));
      if (data != nullptr && message != nullptr) {
        data->longshot_preview = std::move(message->image);
        updateOverlay(hwnd, data);
      }
      return 0;
    }
    case kMsgLongShotFinished: {
      std::unique_ptr<LongShotFinishedMessage> message(
          reinterpret_cast<LongShotFinishedMessage*>(wparam));
      if (data != nullptr) {
        const bool success = message != nullptr && message->success;
        const SelectionAction pending_action = data->longshot_pending_action;
        const bool run_pending_action =
            success && pending_action != SelectionAction::None;
        if (!transitionOverlayPhase(data->phase, OverlayPhase::Selected)) {
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
          return 0;
        }
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
    case kMsgOverlayAbort: {
      if (data != nullptr) {
        data->callback = {};
        data->longshot_control_callback = {};
        data->controller.cancel();
        data->action = SelectionAction::None;
      }
      PostMessageW(hwnd, WM_CLOSE, 0, 0);
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
        SelectionCallback callback = data->callback;
        if (callback && result.action != SelectionAction::LongShot) {
          callback(result);
        }
      }
      return 0;
    }
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

Image makeLongShotPreviewImage(const Image& source) {
  if (source.empty()) {
    return Image{};
  }

  constexpr int kMaxWidth = 440;
  constexpr int kMaxHeight = 720;
  const int divisor = (std::max)(
      1, (std::max)((source.width + kMaxWidth - 1) / kMaxWidth,
                    (source.height + kMaxHeight - 1) / kMaxHeight));
  const int width = (std::max)(1, source.width / divisor);
  const int height = (std::max)(1, source.height / divisor);

  Image result;
  result.width = width;
  result.height = height;
  result.pixels.resize(static_cast<std::size_t>(width) *
                       static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    const int source_y = (std::min)(source.height - 1, y * divisor);
    for (int x = 0; x < width; ++x) {
      const int source_x = (std::min)(source.width - 1, x * divisor);
      result.pixels[static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(width) +
                    static_cast<std::size_t>(x)] =
          source.pixels[static_cast<std::size_t>(source_y) *
                            static_cast<std::size_t>(source.width) +
                        static_cast<std::size_t>(source_x)];
    }
  }
  return result;
}

}  // namespace

bool SelectionOverlay::show(const Image& background,
                             SelectionCallback callback,
                             LongShotControlCallback
                                 longshot_control_callback,
                             const SelectionResult& initial_selection) {
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

  OverlayWindowData data;
  data.background = background;  // 桌面截图背景（物理像素）；空则纯遮罩
  data.callback = std::move(callback);
  data.longshot_control_callback = std::move(longshot_control_callback);
  data.screen = coord::getVirtualScreen();
  data.controller.setBounds(data.screen.width, data.screen.height);

  if (!initial_selection.cancelled && initial_selection.width > 0 &&
      initial_selection.height > 0) {
    data.controller.setSelection(
        coord::screenToClientX(initial_selection.x, data.screen),
        coord::screenToClientY(initial_selection.y, data.screen),
        initial_selection.width, initial_selection.height);
    if (!data.controller.selection().cancelled) {
      (void)transitionOverlayPhase(data.phase, OverlayPhase::Selected);
      data.annotated_image = initial_selection.annotated_image;
    }
  }

  // 按 DPI 缩放 UI（100% 为 96 DPI）：手柄命中半径。
  data.dpi = coord::getSystemDpi();
  const auto scale = [&](int value) { return MulDiv(value, data.dpi, 96); };
  data.handle_radius = scale(handles::kHandleHitRadius);
  data.controller.setHandleRadius(data.handle_radius);

  HWND hwnd = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
      kOverlayClassName, L"", WS_POPUP | WS_VISIBLE, data.screen.left,
      data.screen.top, data.screen.width, data.screen.height, nullptr, nullptr,
      instance, &data);
  if (hwnd == nullptr) {
    return false;
  }
  overlay_hwnd_.store(reinterpret_cast<std::uintptr_t>(hwnd));

  // 遮罩不抢前台激活权：WS_EX_NOACTIVATE 保证点击/显示都不会激活遮罩，
  // 原前台窗口保持激活，其从属浮层（owned popup）不会因失活而隐藏。
  // 键盘取消改由临时全局 Esc 热键提供（模态循环退出后注销）。
  const bool esc_registered =
      RegisterHotKey(hwnd, kEscapeHotkeyId, 0, VK_ESCAPE) != FALSE;

  // 首帧渲染（UpdateLayeredWindow 需要窗口可见）。
  PostMessageW(hwnd, kMsgOverlayReady, 0, 0);

  // 模态消息循环：只以当前 Overlay 的生命周期作为退出条件，不借用线程级
  // WM_QUIT。若应用正在退出，则销毁 Overlay 后把 WM_QUIT 重新交还外层循环。
  bool quit_requested = false;
  bool message_error = false;
  int quit_code = 0;
  MSG msg{};
  while (data.phase != OverlayPhase::Closing) {
    const BOOL message_result = GetMessageW(&msg, nullptr, 0, 0);
    if (message_result == -1) {
      message_error = true;
      break;
    }
    if (message_result == 0) {
      quit_requested = true;
      quit_code = static_cast<int>(msg.wParam);
      break;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  if (esc_registered && IsWindow(hwnd)) {
    UnregisterHotKey(hwnd, kEscapeHotkeyId);
  }

  overlay_hwnd_.store(0);
  if (data.phase != OverlayPhase::Closing && IsWindow(hwnd)) {
    if (overlayPhaseIsLongShot(data.phase) &&
        data.longshot_control_callback) {
      data.longshot_control_callback(LongShotControl::Stop);
    }
    // Application shutdown and message-loop errors must not dispatch the
    // partially selected region through the normal capture callback.
    data.callback = {};
    data.longshot_control_callback = {};
    DestroyWindow(hwnd);
  }

  if (quit_requested) {
    PostQuitMessage(quit_code);
  }

  return !quit_requested && !message_error;
}

bool SelectionOverlay::postLongShotPreview(const Image& image) {
  const HWND hwnd = reinterpret_cast<HWND>(overlay_hwnd_.load());
  if (hwnd == nullptr) {
    return false;
  }
  auto* message = new (std::nothrow) LongShotPreviewMessage;
  if (message == nullptr) {
    return false;
  }
  message->image = makeLongShotPreviewImage(image);
  if (!PostMessageW(hwnd, kMsgLongShotPreview, reinterpret_cast<WPARAM>(message),
                    0)) {
    delete message;
    return false;
  }
  return true;
}

bool SelectionOverlay::postLongShotFinished(bool success) {
  const HWND hwnd = reinterpret_cast<HWND>(overlay_hwnd_.load());
  if (hwnd == nullptr) {
    return false;
  }
  auto* message = new (std::nothrow) LongShotFinishedMessage;
  if (message == nullptr) {
    return false;
  }
  message->success = success;
  if (!PostMessageW(hwnd, kMsgLongShotFinished,
                    reinterpret_cast<WPARAM>(message), 0)) {
    delete message;
    return false;
  }
  return true;
}

void SelectionOverlay::hide() {
  const HWND hwnd = reinterpret_cast<HWND>(overlay_hwnd_.load());
  if (hwnd != nullptr && IsWindow(hwnd)) {
    PostMessageW(hwnd, kMsgOverlayAbort, 0, 0);
  }
}

}  // namespace qingying
