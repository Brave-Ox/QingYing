#include "qingying/action/image_memory_budget.hpp"
#include <mutex>
#include <algorithm>
#include <unordered_map>
#include <stdexcept>
#include <utility>

namespace qingying {
namespace {
constexpr std::size_t index(ImageMemoryKind kind) { return static_cast<std::size_t>(kind); }
bool withinKindLimit(const ImageMemorySnapshot& usage, ImageMemoryKind kind, std::uint64_t bytes) {
  std::uint64_t cap = usage.limit_bytes, count = static_cast<std::uint64_t>(-1);
  if (kind == ImageMemoryKind::Preview) { cap = (std::min)(cap, 16ULL * 1024 * 1024); count = 4; }
  if (kind == ImageMemoryKind::VisualCache) { cap = (std::min)(cap, 128ULL * 1024 * 1024); count = 2; }
  return usage.allocations[index(kind)] < count && usage.bytes[index(kind)] <= cap &&
         bytes <= cap - usage.bytes[index(kind)];
}
}
struct ImageMemoryBudget::State {
  struct Allocation { std::uint64_t bytes; ImageMemoryKind kind; };
  std::mutex mutex;
  ImageMemorySnapshot usage;
  std::unordered_map<const void*, Allocation> allocations;
};
ImageMemoryBudget::ImageMemoryBudget(std::uint64_t limit) : state_(std::make_shared<State>()) {
  if (!limit) throw std::invalid_argument("image memory limit");
  state_->usage.limit_bytes = limit;
}
ImageMemoryBudget ImageMemoryBudget::global() { static ImageMemoryBudget budget; return budget; }
ImageMemoryBudget::Token ImageMemoryBudget::reserve(std::uint64_t bytes, ImageMemoryKind kind) const {
  if (index(kind) >= index(ImageMemoryKind::Count)) return {};
  std::lock_guard<std::mutex> lock(state_->mutex);
  auto& usage = state_->usage;
  if (bytes > usage.limit_bytes - usage.used_bytes || !withinKindLimit(usage, kind, bytes)) {
    ++usage.rejected;
    return {};
  }
  Token token;
  token.state_ = state_; token.bytes_ = bytes; token.kind_ = kind;
  usage.used_bytes += bytes;
  usage.bytes[index(kind)] += bytes;
  ++usage.allocations[index(kind)];
  usage.peak[index(kind)] = (std::max)(usage.peak[index(kind)], usage.bytes[index(kind)]);
  usage.peak_bytes = (std::max)(usage.peak_bytes, usage.used_bytes);
  return token;
}
ImageMemoryBudget::Token::~Token() { reset(); }
ImageMemoryBudget::Token::Token(Token&& other) noexcept { *this = std::move(other); }
ImageMemoryBudget::Token& ImageMemoryBudget::Token::operator=(Token&& other) noexcept {
  if (this != &other) {
    reset(); state_ = std::move(other.state_); bytes_ = other.bytes_; kind_ = other.kind_;
  }
  return *this;
}
void ImageMemoryBudget::Token::reset() noexcept {
  if (!state_) return;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->usage.used_bytes -= bytes_;
    state_->usage.bytes[index(kind_)] -= bytes_;
    --state_->usage.allocations[index(kind_)];
  }
  state_.reset();
}
ImageMemorySnapshot ImageMemoryBudget::snapshot() const noexcept {
  std::lock_guard<std::mutex> lock(state_->mutex); return state_->usage;
}
bool ImageMemoryBudget::setLimit(std::uint64_t bytes) noexcept {
  std::lock_guard<std::mutex> lock(state_->mutex);
  if (!bytes || bytes < state_->usage.used_bytes) return false;
  state_->usage.limit_bytes = bytes; return true;
}
void* ImageMemoryBudget::allocate(std::size_t bytes, ImageMemoryKind kind) const {
  auto token = reserve(bytes, kind);
  if (!token) throw std::bad_alloc{};
  void* pointer = ::operator new(bytes);
  try {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->allocations.emplace(pointer, State::Allocation{bytes, kind});
  } catch (...) { ::operator delete(pointer); throw; }
  // Ownership transfers to the allocation registry; release follows free.
  token.state_.reset();
  return pointer;
}
void ImageMemoryBudget::deallocate(void* pointer) const noexcept {
  if (!pointer) return;
  std::lock_guard<std::mutex> lock(state_->mutex);
  const auto found = state_->allocations.find(pointer);
  if (found == state_->allocations.end()) return;
  const auto allocation = found->second;
  ::operator delete(pointer);
  state_->usage.used_bytes -= allocation.bytes;
  state_->usage.bytes[index(allocation.kind)] -= allocation.bytes;
  --state_->usage.allocations[index(allocation.kind)];
  state_->allocations.erase(found);
}
bool ImageMemoryBudget::classify(const void* pointer, ImageMemoryKind kind) const noexcept {
  if (!pointer) return true;
  if (index(kind) >= index(ImageMemoryKind::Count)) return false;
  std::lock_guard<std::mutex> lock(state_->mutex);
  const auto found = state_->allocations.find(pointer);
  if (found == state_->allocations.end()) return false;
  auto& allocation = found->second;
  if (allocation.kind == kind) return true;
  if (!withinKindLimit(state_->usage, kind, allocation.bytes)) {
    ++state_->usage.rejected;
    return false;
  }
  state_->usage.bytes[index(allocation.kind)] -= allocation.bytes;
  --state_->usage.allocations[index(allocation.kind)];
  state_->usage.bytes[index(kind)] += allocation.bytes;
  ++state_->usage.allocations[index(kind)];
  state_->usage.peak[index(kind)] = (std::max)(state_->usage.peak[index(kind)], state_->usage.bytes[index(kind)]);
  allocation.kind = kind;
  return true;
}
}  // namespace qingying
