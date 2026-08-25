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

constexpr DWORD kPinWindowStyle =
    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME;
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

  // The minimum size must not change the image aspect ratio. The paint path
  // still letterboxes the image if the user resizes the window later.
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

  RECT window_rect{0, 0, client_width, client_height};
  if (!AdjustWindowRectEx(&window_rect, kPinWindowStyle, FALSE,
                          kPinWindowExStyle)) {
    return false;
  }

  const int window_width = window_rect.right - window_rect.left;
  const int window_height = window_rect.bottom - window_rect.top;
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

  HBRUSH background = CreateSolidBrush(RGB(32, 32, 32));
  if (background != nullptr) {
    FillRect(dc, &client_rect, background);
    DeleteObject(background);
  }

  const int client_width = client_rect.right - client_rect.left;
  const int client_height = client_rect.bottom - client_rect.top;
  if (client_width <= 0 || client_height <= 0) {
    return;
  }

  const double image_aspect = static_cast<double>(image_.width) /
                              static_cast<double>(image_.height);
  const double client_aspect = static_cast<double>(client_width) /
                               static_cast<double>(client_height);

  int draw_width = client_width;
  int draw_height = client_height;
  if (client_aspect > image_aspect) {
    draw_width = static_cast<int>(std::lround(client_height * image_aspect));
  } else {
    draw_height = static_cast<int>(std::lround(client_width / image_aspect));
  }

  const int draw_x = (client_width - draw_width) / 2;
  const int draw_y = (client_height - draw_height) / 2;

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = image_.width;
  bitmap_info.bmiHeader.biHeight = -image_.height;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  StretchDIBits(dc, draw_x, draw_y, draw_width, draw_height, 0, 0,
                image_.width, image_.height, image_.pixels.data(),
                &bitmap_info, DIB_RGB_COLORS, SRCCOPY);
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
