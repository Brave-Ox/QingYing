#include "qingying/overlay/selection_overlay.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "qingying/overlay/mask_renderer.hpp"
#include "qingying/overlay/selection_controller.hpp"

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

// SelectionController 等 UI 状态通过窗口属性（WindowLongPtr）附加到窗口，
// 供 WndProc 在处理消息时访问，避免全局/静态可变量。
struct OverlayWindowData {
  SelectionController controller;
  SelectionCallback callback;
  HWND overlay{nullptr};
  HWND toolbar{nullptr};
  SelectionAction action{SelectionAction::None};
  bool selection_confirmed{false};
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
      const int y = kToolbarPadding;
      int x = kToolbarPadding;
      const auto createButton = [&](const wchar_t* label, UINT id,
                                    bool enabled) -> HWND {
        HWND button = CreateWindowExW(
            0, L"BUTTON", label, button_style, x, y, kToolbarButtonWidth,
            kToolbarButtonHeight, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance,
            nullptr);
        x += kToolbarButtonWidth + kToolbarButtonGap;
        if (button != nullptr && !enabled) {
          EnableWindow(button, FALSE);
        }
        return button;
      };

      createButton(L"复制", kToolbarButtonCopy, true);
      createButton(L"下载图片", kToolbarButtonSave, true);
      createButton(L"编辑", kToolbarButtonEdit, false);
      createButton(L"钉图", kToolbarButtonPin, false);
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
                 const SelectionResult& selection, int screen_width,
                 int screen_height) {
  if (data == nullptr) {
    return false;
  }

  const int button_count = 4;
  const int toolbar_width =
      kToolbarPadding * 2 + button_count * kToolbarButtonWidth +
      (button_count - 1) * kToolbarButtonGap;
  const int toolbar_height =
      kToolbarPadding * 2 + kToolbarButtonHeight;

  int x = selection.x;
  int y = selection.y + selection.height + 8;
  if (y + toolbar_height > screen_height) {
    y = selection.y - toolbar_height - 8;
  }
  x = (std::max)(0, (std::min)(x, screen_width - toolbar_width));
  y = (std::max)(0, (std::min)(y, screen_height - toolbar_height));

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

// 重新渲染遮罩到分层窗口。返回 false 表示渲染失败。
bool updateOverlay(HWND hwnd, int width, int height,
                   const SelectionResult& selection) {
  HDC screen_dc = GetDC(nullptr);
  if (screen_dc == nullptr) {
    return false;
  }
  std::unique_ptr<HDC__, CompatibleDcDeleter> mem_dc(
      CreateCompatibleDC(screen_dc));
  ReleaseDC(nullptr, screen_dc);
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
  // DIB 与像素缓冲同布局（BGRA，顶向下），直接拷贝。
  std::copy(pixels.begin(), pixels.end(),
            reinterpret_cast<std::uint32_t*>(dib_bits));

  POINT pt_zero{0, 0};
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
  const BOOL ok =
      UpdateLayeredWindow(hwnd, screen_dc, &pt_zero, &size, mem_dc.get(),
                          &pt_src, 0, &blend, ULW_ALPHA);
  SelectObject(mem_dc.get(), old_bitmap);
  return ok != FALSE;
}

// 返回当前屏幕宽/高（主显示器）。
void getScreenSize(int& out_width, int& out_height) {
  out_width = GetSystemMetrics(SM_CXSCREEN);
  out_height = GetSystemMetrics(SM_CYSCREEN);
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
      const SelectionResult& sel = data->controller.selection();
      int width = 0;
      int height = 0;
      getScreenSize(width, height);
      if (!updateOverlay(hwnd, width, height, sel)) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      }
      return 0;
    }
    case WM_LBUTTONDOWN: {
      if (data == nullptr || data->selection_confirmed) {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      SetCapture(hwnd);
      data->controller.begin(x, y);
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (data == nullptr || data->selection_confirmed ||
          (wparam & MK_LBUTTON) == 0) {
        return 0;  // 仅拖拽期间更新
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      data->controller.update(x, y);
      int width = 0;
      int height = 0;
      getScreenSize(width, height);
      updateOverlay(hwnd, width, height, data->controller.selection());
      return 0;
    }
    case WM_LBUTTONUP: {
      if (data == nullptr || data->selection_confirmed) {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      ReleaseCapture();
      data->controller.update(x, y);
      data->controller.confirm();
      const SelectionResult& selection = data->controller.selection();
      if (selection.cancelled) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
      }

      data->selection_confirmed = true;
      int screen_width = 0;
      int screen_height = 0;
      getScreenSize(screen_width, screen_height);
      if (!showToolbar(hwnd, data, selection, screen_width, screen_height)) {
        data->controller.cancel();
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      }
      return 0;
    }
    case WM_RBUTTONDOWN: {
      if (data != nullptr) {
        data->controller.cancel();
        data->action = SelectionAction::None;
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      }
      return 0;
    }
    case WM_KEYDOWN: {
      if (data != nullptr && wparam == VK_ESCAPE) {
        data->controller.cancel();
        data->action = SelectionAction::None;
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
        SelectionResult result = data->controller.selection();
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

  int screen_w = 0;
  int screen_h = 0;
  getScreenSize(screen_w, screen_h);

  HWND hwnd = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kOverlayClassName, L"",
      WS_POPUP | WS_VISIBLE, 0, 0, screen_w, screen_h, nullptr, nullptr,
      instance, &data);
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
