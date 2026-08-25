#include "qingying/pin/pin_window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <utility>

namespace qingying {

namespace {

constexpr wchar_t kPinWindowClassName[] = L"QingYingPinWindow";
constexpr wchar_t kPinWindowTitle[] = L"QingYing Pin";
constexpr int kMaxClientWidth = 800;
constexpr int kMaxClientHeight = 600;
constexpr int kMinClientWidth = 160;
constexpr int kMinClientHeight = 120;
constexpr int kBorderThickness = 4;
constexpr int kResizeBorder = 8;
constexpr int kCloseButtonSize = 28;

constexpr DWORD kPinWindowStyle = WS_POPUP | WS_THICKFRAME;
constexpr DWORD kPinWindowExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;

bool isValidImage(const Image& image) {
  if (image.width <= 0 || image.height <= 0) {
    return false;
  }

  const std::size_t width = static_cast<std::size_t>(image.width);
  const std::size_t height = static_cast<std::size_t>(image.height);
  return image.pixels.size() == width * height;
}

void calculateInitialClientSize(const Image& image, int& width, int& height) {
  const double scale_x = static_cast<double>(kMaxClientWidth) /
                         static_cast<double>(image.width);
  const double scale_y = static_cast<double>(kMaxClientHeight) /
                         static_cast<double>(image.height);
  const double scale = (std::min)(1.0, (std::min)(scale_x, scale_y));

  width = (std::max)(kMinClientWidth,
                     static_cast<int>(std::lround(image.width * scale)));
  height = (std::max)(kMinClientHeight,
                      static_cast<int>(std::lround(image.height * scale)));

  // The minimum size must not change the image aspect ratio. WM_SIZING keeps
  // this ratio when the user resizes the window later.
  const double aspect = static_cast<double>(image.width) /
                        static_cast<double>(image.height);
  if (static_cast<double>(width) / static_cast<double>(height) > aspect) {
    width = static_cast<int>(std::lround(height * aspect));
  } else {
    height = static_cast<int>(std::lround(width / aspect));
  }
  width = (std::max)(1, width);
  height = (std::max)(1, height);
}

}  // namespace

PinWindow::PinWindow(Image image) : image_(std::move(image)) {}

PinWindow::~PinWindow() {
  close();
}

bool PinWindow::registerWindowClass() {
  static std::once_flag once;
  static bool registered = false;
  std::call_once(once, [] {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(WNDCLASSEXW);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = &PinWindow::windowProc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kPinWindowClassName;

    const ATOM atom = RegisterClassExW(&window_class);
    registered = atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  });
  return registered;
}

void PinWindow::setClosedCallback(ClosedCallback callback) {
  closed_callback_ = std::move(callback);
}

bool PinWindow::show() {
  if (hwnd_ != nullptr || !isValidImage(image_) ||
      !registerWindowClass()) {
    return false;
  }

  int client_width = 0;
  int client_height = 0;
  calculateInitialClientSize(image_, client_width, client_height);

  // WM_NCCALCSIZE makes the client area cover the complete popup, so no
  // native caption/frame pixels need to be added to the requested size.
  const int window_width = client_width;
  const int window_height = client_height;
  const int screen_width = GetSystemMetrics(SM_CXSCREEN);
  const int screen_height = GetSystemMetrics(SM_CYSCREEN);
  const int x = (std::max)(0, (screen_width - window_width) / 2);
  const int y = (std::max)(0, (screen_height - window_height) / 2);

  closing_ = true;
  hwnd_ = CreateWindowExW(
      kPinWindowExStyle, kPinWindowClassName, kPinWindowTitle,
      kPinWindowStyle, x, y, window_width, window_height, nullptr, nullptr,
      GetModuleHandleW(nullptr), this);
  if (hwnd_ == nullptr) {
    closing_ = false;
    return false;
  }

  closing_ = false;
  ShowWindow(hwnd_, SW_SHOWNORMAL);
  SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
  UpdateWindow(hwnd_);
  return true;
}

void PinWindow::close() {
  if (hwnd_ == nullptr) {
    return;
  }

  closing_ = true;
  DestroyWindow(hwnd_);
  closing_ = false;
}

void PinWindow::paint(HDC dc) {
  RECT client_rect{};
  GetClientRect(hwnd_, &client_rect);

  HBRUSH background = CreateSolidBrush(RGB(18, 18, 18));
  if (background != nullptr) {
    FillRect(dc, &client_rect, background);
    DeleteObject(background);
  }

  RECT image_rect = client_rect;
  image_rect.left += kBorderThickness;
  image_rect.top += kBorderThickness;
  image_rect.right -= kBorderThickness;
  image_rect.bottom -= kBorderThickness;

  const int image_width = image_rect.right - image_rect.left;
  const int image_height = image_rect.bottom - image_rect.top;
  if (image_width <= 0 || image_height <= 0) {
    return;
  }

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = image_.width;
  bitmap_info.bmiHeader.biHeight = -image_.height;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  // WM_SIZING keeps the client area at the image aspect ratio, so filling
  // the image area scales the complete screenshot without cropping or bars.
  StretchDIBits(dc, image_rect.left, image_rect.top, image_width,
                image_height, 0, 0, image_.width, image_.height,
                image_.pixels.data(),
                &bitmap_info, DIB_RGB_COLORS, SRCCOPY);

  HBRUSH border = CreateSolidBrush(RGB(255, 82, 82));
  if (border != nullptr) {
    for (int i = 0; i < kBorderThickness; ++i) {
      RECT border_rect{client_rect.left + i, client_rect.top + i,
                       client_rect.right - i, client_rect.bottom - i};
      FrameRect(dc, &border_rect, border);
    }
    DeleteObject(border);
  }

  const RECT close_rect = closeButtonRect();
  HBRUSH close_background = CreateSolidBrush(RGB(70, 70, 70));
  if (close_background != nullptr) {
    FillRect(dc, &close_rect, close_background);
    DeleteObject(close_background);
  }

  HPEN close_pen = CreatePen(PS_SOLID, 2, RGB(235, 235, 235));
  if (close_pen != nullptr) {
    const HGDIOBJ old_pen = SelectObject(dc, close_pen);
    const int margin = 9;
    MoveToEx(dc, close_rect.left + margin, close_rect.top + margin, nullptr);
    LineTo(dc, close_rect.right - margin, close_rect.bottom - margin);
    MoveToEx(dc, close_rect.right - margin, close_rect.top + margin, nullptr);
    LineTo(dc, close_rect.left + margin, close_rect.bottom - margin);
    SelectObject(dc, old_pen);
    DeleteObject(close_pen);
  }
}

RECT PinWindow::closeButtonRect() const {
  RECT client_rect{};
  GetClientRect(hwnd_, &client_rect);
  return RECT{client_rect.right - kBorderThickness - kCloseButtonSize,
              client_rect.top + kBorderThickness,
              client_rect.right - kBorderThickness,
              client_rect.top + kBorderThickness + kCloseButtonSize};
}

LRESULT PinWindow::hitTest(POINT point) const {
  const RECT close_rect = closeButtonRect();
  if (PtInRect(&close_rect, point)) {
    return HTCLIENT;
  }

  RECT client_rect{};
  GetClientRect(hwnd_, &client_rect);
  const bool left = point.x < client_rect.left + kResizeBorder;
  const bool right = point.x >= client_rect.right - kResizeBorder;
  const bool top = point.y < client_rect.top + kResizeBorder;
  const bool bottom = point.y >= client_rect.bottom - kResizeBorder;

  if (top && left) {
    return HTTOPLEFT;
  }
  if (top && right) {
    return HTTOPRIGHT;
  }
  if (bottom && left) {
    return HTBOTTOMLEFT;
  }
  if (bottom && right) {
    return HTBOTTOMRIGHT;
  }
  if (left) {
    return HTLEFT;
  }
  if (right) {
    return HTRIGHT;
  }
  if (top) {
    return HTTOP;
  }
  if (bottom) {
    return HTBOTTOM;
  }
  return HTCAPTION;
}

void PinWindow::handleSizing(WPARAM edge, RECT* window_rect) const {
  if (window_rect == nullptr || image_.width <= 0 || image_.height <= 0) {
    return;
  }

  const double aspect = static_cast<double>(image_.width) /
                        static_cast<double>(image_.height);
  int width = window_rect->right - window_rect->left;
  int height = window_rect->bottom - window_rect->top;
  width = (std::max)(width, kMinClientWidth);
  height = (std::max)(height, kMinClientHeight);

  const auto setWidthFromHeight = [&] {
    width = (std::max)(kMinClientWidth,
                       static_cast<int>(std::lround(height * aspect)));
  };
  const auto setHeightFromWidth = [&] {
    height = (std::max)(kMinClientHeight,
                        static_cast<int>(std::lround(width / aspect)));
  };

  switch (edge) {
    case WMSZ_LEFT:
      setHeightFromWidth();
      window_rect->left = window_rect->right - width;
      window_rect->bottom = window_rect->top + height;
      break;
    case WMSZ_RIGHT:
      setHeightFromWidth();
      window_rect->right = window_rect->left + width;
      window_rect->bottom = window_rect->top + height;
      break;
    case WMSZ_TOP:
      setWidthFromHeight();
      window_rect->top = window_rect->bottom - height;
      window_rect->right = window_rect->left + width;
      break;
    case WMSZ_BOTTOM:
      setWidthFromHeight();
      window_rect->right = window_rect->left + width;
      window_rect->bottom = window_rect->top + height;
      break;
    case WMSZ_TOPLEFT:
      if (static_cast<double>(width) / static_cast<double>(height) > aspect) {
        setWidthFromHeight();
      } else {
        setHeightFromWidth();
      }
      window_rect->left = window_rect->right - width;
      window_rect->top = window_rect->bottom - height;
      break;
    case WMSZ_TOPRIGHT:
      if (static_cast<double>(width) / static_cast<double>(height) > aspect) {
        setWidthFromHeight();
      } else {
        setHeightFromWidth();
      }
      window_rect->right = window_rect->left + width;
      window_rect->top = window_rect->bottom - height;
      break;
    case WMSZ_BOTTOMLEFT:
      if (static_cast<double>(width) / static_cast<double>(height) > aspect) {
        setWidthFromHeight();
      } else {
        setHeightFromWidth();
      }
      window_rect->left = window_rect->right - width;
      window_rect->bottom = window_rect->top + height;
      break;
    case WMSZ_BOTTOMRIGHT:
      if (static_cast<double>(width) / static_cast<double>(height) > aspect) {
        setWidthFromHeight();
      } else {
        setHeightFromWidth();
      }
      window_rect->right = window_rect->left + width;
      window_rect->bottom = window_rect->top + height;
      break;
    default:
      break;
  }
}

void PinWindow::handleDestroyed() {
  hwnd_ = nullptr;
  if (closing_ || !closed_callback_) {
    return;
  }

  ClosedCallback callback = std::move(closed_callback_);
  callback(this);
}

LRESULT CALLBACK PinWindow::windowProc(HWND hwnd, UINT message,
                                       WPARAM wparam, LPARAM lparam) {
  PinWindow* self = reinterpret_cast<PinWindow*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (message) {
    case WM_NCCREATE: {
      const CREATESTRUCTW* create =
          reinterpret_cast<const CREATESTRUCTW*>(lparam);
      self = static_cast<PinWindow*>(create->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(self));
      if (self != nullptr) {
        self->hwnd_ = hwnd;
      }
      return TRUE;
    }
    case WM_PAINT: {
      if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wparam, lparam);
      }
      PAINTSTRUCT paint_struct{};
      HDC dc = BeginPaint(hwnd, &paint_struct);
      if (dc != nullptr) {
        self->paint(dc);
        EndPaint(hwnd, &paint_struct);
      }
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_NCCALCSIZE:
      if (wparam != FALSE) {
        // Remove the native non-client frame. The custom red border is drawn
        // by paint() and WM_NCHITTEST still provides resize hit targets.
        return 0;
      }
      break;
    case WM_NCPAINT:
      // There is no native frame to paint after WM_NCCALCSIZE above.
      return 0;
    case WM_NCHITTEST: {
      if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wparam, lparam);
      }
      POINT point{static_cast<int>(static_cast<short>(LOWORD(lparam))),
                  static_cast<int>(static_cast<short>(HIWORD(lparam)))};
      ScreenToClient(hwnd, &point);
      return self->hitTest(point);
    }
    case WM_SIZING:
      if (self != nullptr) {
        self->handleSizing(wparam, reinterpret_cast<RECT*>(lparam));
        return TRUE;
      }
      break;
    case WM_LBUTTONDOWN: {
      if (self != nullptr) {
        POINT point{static_cast<int>(static_cast<short>(LOWORD(lparam))),
                    static_cast<int>(static_cast<short>(HIWORD(lparam)))};
        const RECT close_rect = self->closeButtonRect();
        if (PtInRect(&close_rect, point)) {
          DestroyWindow(hwnd);
          return 0;
        }
      }
      break;
    }
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_NCDESTROY:
      if (self != nullptr) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->handleDestroyed();
      }
      return 0;
    default:
      break;
  }

  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace qingying
