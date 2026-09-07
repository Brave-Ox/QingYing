#include "qingying/app/result_budget.h"

#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace qingying {

struct ResultBudget::State {
  explicit State(AutomationLimits policy) : limits(policy) {}
  AutomationLimits limits;
  std::mutex mutex;
  ResultBudgetSnapshot usage;
};

ResultBudget::ResultBudget(AutomationLimits limits)
    : state_(std::make_shared<State>(limits)) {
  if (!limits.valid()) throw std::invalid_argument("invalid result budget limits");
}

std::optional<std::uint64_t> ResultBudget::imageBytes(int width, int height) noexcept {
  if (width <= 0 || height <= 0) return std::nullopt;
  const auto w = static_cast<std::uint64_t>(width);
  const auto h = static_cast<std::uint64_t>(height);
  const auto max = (std::numeric_limits<std::uint64_t>::max)();
  if (w > max / h || w * h > max / sizeof(std::uint32_t)) return std::nullopt;
  const auto bytes = w * h * sizeof(std::uint32_t);
  if (bytes > (std::numeric_limits<std::size_t>::max)()) return std::nullopt;
  return bytes;
}

ResultBudget::Reservation ResultBudget::reserve(
    ResultScopeId scope, int width, int height, bool ordinary_capture,
    std::uint64_t capacity_bytes) {
  const auto logical = imageBytes(width, height);
  if (!logical || scope == kInvalidResultScopeId) return {};
  const auto bytes = capacity_bytes == 0 ? *logical : capacity_bytes;
  if (bytes < *logical) return {};
  const bool external = scope != kGuiResultScopeId;
  const auto& limits = state_->limits;
  if (external && (bytes > limits.max_result_bytes ||
      (ordinary_capture && *logical / sizeof(std::uint32_t) > limits.max_capture_pixels))) {
    return {};
  }
  std::lock_guard<std::mutex> lock(state_->mutex);
  auto& usage = state_->usage;
  const auto max = (std::numeric_limits<std::uint64_t>::max)();
  // The invariant reserved + retained <= max makes subtraction safe.
  if (bytes > max - usage.retained_bytes - usage.reserved_bytes) return {};
  if (external && bytes > limits.max_retained_result_bytes -
      usage.external_retained_bytes - usage.external_reserved_bytes) return {};
  Reservation reservation;
  reservation.state_ = state_;
  reservation.bytes_ = bytes;
  reservation.scope_ = scope;
  reservation.width_ = width;
  reservation.height_ = height;
  usage.reserved_bytes += bytes;
  if (external) usage.external_reserved_bytes += bytes;
  return reservation;
}

ResultBudgetSnapshot ResultBudget::snapshot() const noexcept {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->usage;
}

bool ResultBudget::owns(const Reservation& reservation) const noexcept {
  return reservation.state_ == state_ && !reservation.committed_;
}

ResultBudget::Reservation::~Reservation() { reset(); }
ResultBudget::Reservation::Reservation(Reservation&& other) noexcept {
  *this = std::move(other);
}
ResultBudget::Reservation& ResultBudget::Reservation::operator=(Reservation&& other) noexcept {
  if (this != &other) {
    reset();
    state_ = std::move(other.state_);
    bytes_ = other.bytes_;
    scope_ = other.scope_;
    width_ = other.width_;
    height_ = other.height_;
    committed_ = other.committed_;
  }
  return *this;
}
void ResultBudget::Reservation::reset() noexcept {
  if (!state_) return;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto& usage = state_->usage;
    (committed_ ? usage.retained_bytes : usage.reserved_bytes) -= bytes_;
    if (scope_ != kGuiResultScopeId) {
      (committed_ ? usage.external_retained_bytes : usage.external_reserved_bytes) -= bytes_;
    }
  }
  state_.reset();
}
void ResultBudget::Reservation::commit() noexcept {
  std::lock_guard<std::mutex> lock(state_->mutex);
  auto& usage = state_->usage;
  usage.reserved_bytes -= bytes_;
  usage.retained_bytes += bytes_;
  if (scope_ != kGuiResultScopeId) {
    usage.external_reserved_bytes -= bytes_;
    usage.external_retained_bytes += bytes_;
  }
  committed_ = true;
}

}  // namespace qingying
