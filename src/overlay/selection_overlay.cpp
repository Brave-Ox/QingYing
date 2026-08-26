#include "qingying/overlay/selection_overlay.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "qingying/overlay/coordinate_transform.hpp"
#include "qingying/overlay/mask_renderer.hpp"
#include "qingying/overlay/selection_controller.hpp"
#include "qingying/overlay/selection_handles.hpp"
#include "qingying/window/window_detector.hpp"

namespace qingying {

namespace {

// 遮罩窗口的自定义消息：通知窗口属性已就绪，可开始渲染首帧。
constexpr UINT kMsgOverlayReady = WM_APP + 1;
constexpr UINT kToolbarButtonCopy = 1;
constexpr UINT kToolbarButtonSave = 2;
constexpr UINT kToolbarButtonEdit = 3;
constexpr UINT kToolbarButtonPin = 4;
constexpr int kToolbarButtonWidth = 82;
constexpr int kToolbarButtonHeight = 30;
constexpr int kToolbarButtonGap = 4;
constexpr int kToolbarPadding = 4;
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
  HWND toolbar{nullptr};
  SelectionAction action{SelectionAction::None};
  bool selection_confirmed{false};
  DragKind drag{DragKind::None};
  coord::VirtualScreenRect screen;
  // 按 DPI 缩放后的 UI 尺寸（100% 时等于基准常量）。
  int dpi{96};
  int handle_radius{handles::kHandleHitRadius};
  int toolbar_button_w{kToolbarButtonWidth};
  int toolbar_button_h{kToolbarButtonHeight};
  int toolbar_button_gap{kToolbarButtonGap};
  int toolbar_padding{kToolbarPadding};
  SelectionHandle active_handle{SelectionHandle::None};
  WindowDetector window_detector;  // 窗口吸附检测
  bool has_hover{false};           // 是否悬停在可吸附窗口上
  SelectionResult hover_rect;      // 悬停窗口矩形（客户区坐标）
};

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
const wchar_t kToolbarClassName[] = L"QingYingSelectionToolbar";

void destroyToolbar(OverlayWindowData* data) {
  if (data == nullptr || data->toolbar == nullptr) {
    return;
  }
  const HWND toolbar = data->toolbar;
  data->toolbar = nullptr;
  DestroyWindow(toolbar);
}

void chooseToolbarAction(OverlayWindowData* data, SelectionAction action) {
  if (data == nullptr || data->overlay == nullptr) {
    return;
  }
  data->action = action;
  PostMessageW(data->overlay, WM_CLOSE, 0, 0);
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

  SelectionHandle handle = SelectionHandle::None;
  if (data->drag == DragKind::Resize || data->drag == DragKind::Move) {
    handle = data->active_handle;
  } else if (data->selection_confirmed) {
    handle = data->controller.hitTest(x, y);
  }

  HCURSOR cursor = loadOverlayCursor(handle, data->selection_confirmed);
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

LRESULT CALLBACK toolbarWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                LPARAM lparam) {
  OverlayWindowData* data = reinterpret_cast<OverlayWindowData*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (msg) {
    case WM_NCCREATE: {
      const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
      data = static_cast<OverlayWindowData*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(data));
      if (data != nullptr) {
        data->toolbar = hwnd;
      }
      return TRUE;
    }
    case WM_CREATE: {
      if (data == nullptr) {
        return -1;
      }
      const HINSTANCE instance = GetModuleHandleW(nullptr);
      const DWORD button_style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;
      const int y = data->toolbar_padding;
      int x = data->toolbar_padding;
      const auto createButton = [&](const wchar_t* label, UINT id,
                                    bool enabled) -> HWND {
        HWND button = CreateWindowExW(
            0, L"BUTTON", label, button_style, x, y, data->toolbar_button_w,
            data->toolbar_button_h, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance,
            nullptr);
        x += data->toolbar_button_w + data->toolbar_button_gap;
        if (button != nullptr && !enabled) {
          EnableWindow(button, FALSE);
        }
        return button;
      };

      createButton(L"复制", kToolbarButtonCopy, true);
      createButton(L"下载图片", kToolbarButtonSave, true);
      createButton(L"编辑", kToolbarButtonEdit, false);
      createButton(L"钉图", kToolbarButtonPin, true);
      return 0;
    }
    case WM_COMMAND: {
      if (data == nullptr || HIWORD(wparam) != BN_CLICKED) {
        return 0;
      }
      switch (LOWORD(wparam)) {
        case kToolbarButtonCopy:
          chooseToolbarAction(data, SelectionAction::Copy);
          break;
        case kToolbarButtonSave:
          chooseToolbarAction(data, SelectionAction::Save);
          break;
        case kToolbarButtonPin:
          chooseToolbarAction(data, SelectionAction::Pin);
          break;
        default:
          break;
      }
      return 0;
    }
    case WM_KEYDOWN:
      if (data != nullptr && wparam == VK_ESCAPE) {
        data->controller.cancel();
        data->action = SelectionAction::None;
        PostMessageW(data->overlay, WM_CLOSE, 0, 0);
      }
      return 0;
    case WM_DESTROY:
      if (data != nullptr && data->toolbar == hwnd) {
        data->toolbar = nullptr;
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool showToolbar(HWND overlay, OverlayWindowData* data,
                 const SelectionResult& selection_screen,
                 const coord::VirtualScreenRect& screen) {
  if (data == nullptr) {
    return false;
  }

  const int button_count = 4;
  const int toolbar_width =
      data->toolbar_padding * 2 + button_count * data->toolbar_button_w +
      (button_count - 1) * data->toolbar_button_gap;
  const int toolbar_height =
      data->toolbar_padding * 2 + data->toolbar_button_h;

  int x = selection_screen.x;
  int y = selection_screen.y + selection_screen.height + 8;
  if (y + toolbar_height > screen.bottom()) {
    y = selection_screen.y - toolbar_height - 8;
  }

  const int min_x = screen.left;
  const int min_y = screen.top;
  const int max_x = screen.right() - toolbar_width;
  const int max_y = screen.bottom() - toolbar_height;
  x = (std::max)(min_x, (std::min)(x, max_x));
  y = (std::max)(min_y, (std::min)(y, max_y));

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  const HWND toolbar = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kToolbarClassName, L"",
      WS_POPUP | WS_VISIBLE, x, y, toolbar_width, toolbar_height, overlay,
      nullptr, instance, data);
  if (toolbar == nullptr) {
    return false;
  }

  SetWindowPos(toolbar, HWND_TOPMOST, x, y, toolbar_width, toolbar_height,
               SWP_SHOWWINDOW);
  SetForegroundWindow(toolbar);
  SetFocus(toolbar);
  return true;
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
  if (data->selection_confirmed && !selection.cancelled) {
    handles::drawHandles(pixels, width, height, selection.x, selection.y,
                         selection.width, selection.height, kHandlePixel,
                         data->handle_radius);
  } else if (data->drag == DragKind::None && data->has_hover) {
    drawHoverOutline(pixels, width, height, data->hover_rect, kHoverPixel,
                     kHoverThickness);
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
      }
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
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));

      if (!data->selection_confirmed) {
        // 尚无有效选区：开始拖出矩形。
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
        data->selection_confirmed = false;
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
        if ((wparam & MK_LBUTTON) == 0 && !data->selection_confirmed) {
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
        data->selection_confirmed = false;
        destroyToolbar(data);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
      }

      data->selection_confirmed = true;
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
        data->controller.cancel();
        data->action = SelectionAction::None;
        data->selection_confirmed = false;
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      }
      return 0;
    }
    case WM_KEYDOWN: {
      if (data != nullptr && wparam == VK_ESCAPE) {
        data->controller.cancel();
        data->action = SelectionAction::None;
        data->selection_confirmed = false;
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      }
      return 0;
    }
    case WM_CLOSE: {
      destroyToolbar(data);
      DestroyWindow(hwnd);
      return 0;
    }
    case WM_DESTROY: {
      if (data != nullptr) {
        destroyToolbar(data);
        SelectionResult result = toScreenSelection(data->controller.selection(),
                                                   data->screen);
        result.action = data->action;
        SelectionCallback callback = data->callback;
        if (callback) {
          callback(result);
        }
      }
      PostQuitMessage(0);
      return 0;
    }
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

bool SelectionOverlay::show(SelectionCallback callback) {
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

  WNDCLASSEXW toolbar_wc{};
  toolbar_wc.cbSize = sizeof(WNDCLASSEXW);
  toolbar_wc.lpfnWndProc = toolbarWndProc;
  toolbar_wc.hInstance = instance;
  toolbar_wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
  toolbar_wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  toolbar_wc.lpszClassName = kToolbarClassName;
  if (RegisterClassExW(&toolbar_wc) == 0 &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  OverlayWindowData data;
  data.callback = std::move(callback);
  data.screen = coord::getVirtualScreen();
  data.controller.setBounds(data.screen.width, data.screen.height);

  // 按 DPI 缩放 UI（100% 为 96 DPI）：手柄命中半径与操作条尺寸。
  data.dpi = coord::getSystemDpi();
  const auto scale = [&](int value) { return MulDiv(value, data.dpi, 96); };
  data.handle_radius = scale(handles::kHandleHitRadius);
  data.toolbar_button_w = scale(kToolbarButtonWidth);
  data.toolbar_button_h = scale(kToolbarButtonHeight);
  data.toolbar_button_gap = scale(kToolbarButtonGap);
  data.toolbar_padding = scale(kToolbarPadding);
  data.controller.setHandleRadius(data.handle_radius);

  HWND hwnd = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kOverlayClassName, L"",
      WS_POPUP | WS_VISIBLE, data.screen.left, data.screen.top,
      data.screen.width, data.screen.height, nullptr, nullptr, instance,
      &data);
  if (hwnd == nullptr) {
    return false;
  }

  // 抢占前台/焦点：保证尚未点击时按 Esc 也能取消（热键回调后原前台窗口
  // 未必让位）。随后首帧渲染（UpdateLayeredWindow 需要窗口可见）。
  SetForegroundWindow(hwnd);
  SetFocus(hwnd);
  PostMessageW(hwnd, kMsgOverlayReady, 0, 0);

  // 模态消息循环：捕获期间阻塞，直到选区确认/取消（WM_DESTROY → PostQuitMessage）。
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  return true;
}

void SelectionOverlay::hide() {
  // 遮罩窗口生命周期由 show() 的模态循环管理；hide() 为接口完整性保留。
}

}  // namespace qingying
