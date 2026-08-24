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

// SelectionController 等 UI 状态通过窗口属性（WindowLongPtr）附加到窗口，
// 供 WndProc 在处理消息时访问，避免全局/静态可变量。
struct OverlayWindowData {
  SelectionController controller;
  SelectionCallback callback;
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
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
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
      if (data == nullptr) {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      SetCapture(hwnd);
      data->controller.begin(x, y);
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (data == nullptr || (wparam & MK_LBUTTON) == 0) {
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
      if (data == nullptr) {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      ReleaseCapture();
      data->controller.update(x, y);
      data->controller.confirm();
      PostMessageW(hwnd, WM_CLOSE, 0, 0);
      return 0;
    }
    case WM_KEYDOWN: {
      if (data != nullptr && wparam == VK_ESCAPE) {
        data->controller.cancel();
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      }
      return 0;
    }
    case WM_CLOSE: {
      DestroyWindow(hwnd);
      return 0;
    }
    case WM_DESTROY: {
      if (data != nullptr) {
        const SelectionResult result = data->controller.selection();
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
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(IDC_CROSS));
  wc.lpszClassName = kOverlayClassName;
  if (RegisterClassExW(&wc) == 0 &&
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
