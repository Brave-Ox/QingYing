#include "qingying/pin/pin_manager.hpp"

#include "qingying/pin/pin_window.hpp"

#include <dwmapi.h>

#include <algorithm>
#include <memory>

namespace qingying {

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
  if (!raw_window->show()) {
    return false;
  }

  windows_.push_back(std::move(window));
  return true;
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
