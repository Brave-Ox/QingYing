#include "qingying/pin/pin_manager.hpp"

#include "qingying/pin/pin_window.hpp"

#include <dwmapi.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
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

PinManager::PinManager(AutomationLimits limits, WindowPresenter presenter)
    : limits_(limits), presenter_(std::move(presenter)) {
  if (!limits_.valid()) throw std::invalid_argument("pin limits");
}

PinManager::~PinManager() {
  closeAll();
}

bool PinManager::show(const Image& image) {
  return static_cast<bool>(showGui(image));
}

PinManager::CreationResult PinManager::showGui(const Image& image) {
  return create(image, PinSource::Gui, {});
}

PinManager::CreationResult PinManager::showAgent(
    const Image& image, CommitAuthorization authorize_commit) {
  return create(image, PinSource::Agent, std::move(authorize_commit));
}

PinManager::CreationResult PinManager::create(
    const Image& image, PinSource source,
    CommitAuthorization authorize_commit) {
  if (image.width <= 0 || image.height <= 0 || image.pixels.empty()) {
    return {ErrorCode::kInvalidArgument};
  }

  const std::size_t expected_pixels =
      static_cast<std::size_t>(image.width) *
      static_cast<std::size_t>(image.height);
  if (image.pixels.size() != expected_pixels) {
    return {ErrorCode::kInvalidArgument};
  }

  const bool agent = source == PinSource::Agent;
  std::uint64_t agent_bytes = 0;
  if (agent) {
    if (image.pixels.size() >
        (std::numeric_limits<std::uint64_t>::max)() / sizeof(std::uint32_t))
      return {ErrorCode::kResourceLimit};
    agent_bytes = static_cast<std::uint64_t>(image.pixels.size()) *
                  sizeof(std::uint32_t);
    if (agent_usage_.count >= limits_.max_agent_pins ||
        agent_usage_.bytes > limits_.max_agent_pin_bytes ||
        agent_bytes > limits_.max_agent_pin_bytes - agent_usage_.bytes)
      return {ErrorCode::kResourceLimit};
    ++agent_usage_.count;
    agent_usage_.bytes += agent_bytes;
  }

  if (authorize_commit && !authorize_commit()) {
    if (agent) releaseAgentBudget(agent_bytes);
    return {ErrorCode::kCancelled};
  }

  try {
    const PinId pin_id = allocatePinId();
    if (pin_id == kInvalidPinId) {
      if (agent) releaseAgentBudget(agent_bytes);
      return {ErrorCode::kResourceLimit};
    }
    auto window = std::make_unique<PinWindow>(image, pin_id, source);
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

    const bool shown = presenter_ ? presenter_(*raw_window, x, y)
                                  : raw_window->show(x, y);
    if (!shown) {
      if (agent) releaseAgentBudget(agent_bytes);
      return {ErrorCode::kUnknown};
    }

    windows_.push_back({std::move(window), agent_bytes, agent});
    return {ErrorCode::kOk, pin_id};
  } catch (...) {
    if (agent) releaseAgentBudget(agent_bytes);
    return {ErrorCode::kUnknown};
  }
}

std::vector<RECT> PinManager::windowRects() const {
  std::vector<RECT> rects;
  rects.reserve(windows_.size());
  for (const auto& entry : windows_) {
    const auto& window = entry.window;
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
    for (const auto& entry : windows_) {
      const auto& existing = entry.window;
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
    Entry entry = std::move(windows_.back());
    windows_.pop_back();
    if (entry.agent_accounted) releaseAgentBudget(entry.agent_bytes);
    entry.window->close();
  }
}

int PinManager::count() const {
  return static_cast<int>(windows_.size());
}

bool PinManager::contains(PinId pin_id) const noexcept {
  return std::any_of(windows_.begin(), windows_.end(),
      [pin_id](const Entry& entry) {
        return entry.window->pinId() == pin_id;
      });
}

std::optional<PinSource> PinManager::source(PinId pin_id) const noexcept {
  const auto it = std::find_if(windows_.begin(), windows_.end(),
      [pin_id](const Entry& entry) {
        return entry.window->pinId() == pin_id;
      });
  return it == windows_.end() ? std::nullopt
                              : std::optional<PinSource>{it->window->source()};
}

void PinManager::setActionCallbacks(ImageActionCallback copy_callback,
                                    ImageActionCallback save_callback) {
  copy_callback_ = std::move(copy_callback);
  save_callback_ = std::move(save_callback);
  for (const auto& entry : windows_) {
    entry.window->setActionCallbacks(copy_callback_, save_callback_);
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
  for (const auto& entry : windows_) {
    const HWND hwnd = entry.window->hwnd();
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
      [window](const Entry& item) {
        return item.window.get() == window;
      });
  if (it != windows_.end()) {
    if (it->agent_accounted) releaseAgentBudget(it->agent_bytes);
    windows_.erase(it);
  }
}

PinId PinManager::allocatePinId() noexcept {
  if (next_pin_id_ == kInvalidPinId) return kInvalidPinId;
  return next_pin_id_++;
}

void PinManager::releaseAgentBudget(std::uint64_t bytes) noexcept {
  if (agent_usage_.count == 0 || bytes > agent_usage_.bytes) return;
  --agent_usage_.count;
  agent_usage_.bytes -= bytes;
}

}  // namespace qingying
