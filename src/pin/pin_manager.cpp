#include "qingying/pin/pin_manager.hpp"

#include "qingying/pin/pin_window.hpp"

#include <dwmapi.h>

#include <algorithm>
#include <memory>
#include <utility>

namespace qingying {

namespace {

// 钉图自动排列参数：屏幕边缘间距与钉图之间的间距（物理像素）。
constexpr int kPinMargin = 24;
constexpr int kPinGap = 16;
// 安全上限：屏幕再大，行数也不会超过此值，防止极端情况死循环。
constexpr int kMaxLayoutRows = 64;

bool rectsOverlap(const RECT& a, const RECT& b) {
  return a.left < b.right && a.right > b.left && a.top < b.bottom &&
         a.bottom > b.top;
}

}  // namespace

PinManager::CaptureGuard::CaptureGuard(PinManager& manager)
    : manager_(&manager) {
  manager_->beginCaptureExclusion();
}

PinManager::CaptureGuard::~CaptureGuard() {
  if (manager_ != nullptr) {
    manager_->endCaptureExclusion();
  }
}

PinManager::~PinManager() {
  closeAll();
}

bool PinManager::show(const Image& image) {
  if (image.width <= 0 || image.height <= 0 || image.pixels.empty()) {
    return false;
  }

  const std::size_t expected_pixels =
      static_cast<std::size_t>(image.width) *
      static_cast<std::size_t>(image.height);
  if (image.pixels.size() != expected_pixels) {
    return false;
  }

  auto window = std::make_unique<PinWindow>(image);
  PinWindow* raw_window = window.get();
  raw_window->setClosedCallback(
      [this](PinWindow* closed_window) { onWindowClosed(closed_window); });
  raw_window->setActionCallbacks(copy_callback_, save_callback_);

  // 先按图片比例估算窗口尺寸，再计算不与已有钉图重叠的初始位置
  // （右侧依次排列），避免多个钉图叠放在同一处。
  int width = 0;
  int height = 0;
  PinWindow::computeInitialClientSize(image, width, height);
  int x = 0;
  int y = 0;
  computeNextPinPosition(width, height, x, y);

  if (!raw_window->show(x, y)) {
    return false;
  }

  windows_.push_back(std::move(window));
  return true;
}

std::vector<RECT> PinManager::windowRects() const {
  std::vector<RECT> rects;
  rects.reserve(windows_.size());
  for (const auto& window : windows_) {
    RECT rect{};
    if (window->hwnd() != nullptr && GetWindowRect(window->hwnd(), &rect)) {
      rects.push_back(rect);
    }
  }
  return rects;
}

void PinManager::computeNextPinPosition(int width, int height, int& out_x,
                                        int& out_y) const {
  const int screen_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int screen_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int screen_right = screen_left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int screen_bottom = screen_top + GetSystemMetrics(SM_CYVIRTUALSCREEN);

  int col = 0;
  int row = 0;
  while (row < kMaxLayoutRows) {
    const int x = screen_right - kPinMargin - width - col * (width + kPinGap);
    const int y = screen_top + kPinMargin + row * (height + kPinGap);
    if (x < screen_left + kPinMargin) {
      // 当前行放不下：换下一行，从右侧重新开始。
      ++row;
      col = 0;
      continue;
    }
    if (y + height > screen_bottom - kPinMargin) {
      // 屏幕纵向放不下：退回当前候选，不再继续寻找。
      break;
    }

    const RECT candidate{x, y, x + width, y + height};
    bool overlap = false;
    for (const auto& existing : windows_) {
      if (existing->hwnd() == nullptr) {
        continue;
      }
      RECT existing_rect{};
      if (GetWindowRect(existing->hwnd(), &existing_rect) &&
          rectsOverlap(candidate, existing_rect)) {
        overlap = true;
        break;
      }
    }
    if (!overlap) {
      out_x = x;
      out_y = y;
      return;
    }
    ++col;
  }

  // 兜底：屏幕右上角（极端情况下可能重叠，但保证有位置返回）。
  out_x = screen_right - kPinMargin - width;
  out_y = screen_top + kPinMargin;
}

void PinManager::closeAll() {
  while (!windows_.empty()) {
    std::unique_ptr<PinWindow> window = std::move(windows_.back());
    windows_.pop_back();
    window->close();
  }
}

int PinManager::count() const {
  return static_cast<int>(windows_.size());
}

void PinManager::setActionCallbacks(ImageActionCallback copy_callback,
                                    ImageActionCallback save_callback) {
  copy_callback_ = std::move(copy_callback);
  save_callback_ = std::move(save_callback);
  for (const auto& window : windows_) {
    window->setActionCallbacks(copy_callback_, save_callback_);
  }
}

PinManager::CaptureGuard PinManager::temporarilyHideForCapture() {
  return CaptureGuard(*this);
}

void PinManager::beginCaptureExclusion() {
  ++capture_exclusion_depth_;
  if (capture_exclusion_depth_ != 1) {
    return;
  }

  hidden_windows_.clear();
  for (const auto& window : windows_) {
    const HWND hwnd = window->hwnd();
    if (hwnd == nullptr || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
      continue;
    }

    hidden_windows_.push_back(hwnd);
    ShowWindow(hwnd, SW_HIDE);
  }

  // Wait for DWM to commit the hidden state before BitBlt reads the desktop.
  // Ignore the return value because classic desktop composition may be off.
  DwmFlush();
}

void PinManager::endCaptureExclusion() {
  if (capture_exclusion_depth_ <= 0) {
    return;
  }
  --capture_exclusion_depth_;
  if (capture_exclusion_depth_ != 0) {
    return;
  }

  for (const HWND hwnd : hidden_windows_) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
      continue;
    }
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }
  hidden_windows_.clear();
}

void PinManager::onWindowClosed(PinWindow* window) {
  const auto it = std::find_if(
      windows_.begin(), windows_.end(),
      [window](const std::unique_ptr<PinWindow>& item) {
        return item.get() == window;
      });
  if (it != windows_.end()) {
    windows_.erase(it);
  }
}

}  // namespace qingying
