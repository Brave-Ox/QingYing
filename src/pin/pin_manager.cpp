#include "qingying/pin/pin_manager.hpp"

#include "qingying/pin/pin_window.hpp"

#include <algorithm>
#include <memory>

namespace qingying {

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
