#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

namespace qingying {
enum class ImageMemoryKind { Retained, InFlight, Preview, WorkerQueue, EncodeScratch,
                             WireCopy, ExternalLease, VisualCache, Count };
struct ImageMemorySnapshot {
  std::uint64_t limit_bytes{0}, used_bytes{0}, peak_bytes{0}, rejected{0};
  std::array<std::uint64_t, static_cast<std::size_t>(ImageMemoryKind::Count)> bytes{};
  std::array<std::uint64_t, static_cast<std::size_t>(ImageMemoryKind::Count)> peak{};
  std::array<std::uint64_t, static_cast<std::size_t>(ImageMemoryKind::Count)> allocations{};
};

// One process-wide physical-allocation ledger. Shared leases do not charge the
// same pixels twice. Tokens and allocator state outlive their initiating scope.
class ImageMemoryBudget {
  struct State;
 public:
  class Token {
   public:
    Token() = default;
    ~Token();
    Token(Token&&) noexcept;
    Token& operator=(Token&&) noexcept;
    Token(const Token&) = delete;
    Token& operator=(const Token&) = delete;
    explicit operator bool() const noexcept { return state_ != nullptr; }
    void reset() noexcept;
   private:
    friend class ImageMemoryBudget;
    std::shared_ptr<State> state_;
    std::uint64_t bytes_{0};
    ImageMemoryKind kind_{ImageMemoryKind::InFlight};
  };
  explicit ImageMemoryBudget(std::uint64_t limit = 512ULL * 1024 * 1024);
  static ImageMemoryBudget global();
  Token reserve(std::uint64_t bytes, ImageMemoryKind kind) const;
  ImageMemorySnapshot snapshot() const noexcept;
  // Live allocations prevent lowering a limit below current usage.
  bool setLimit(std::uint64_t bytes) noexcept;
  void* allocate(std::size_t bytes, ImageMemoryKind kind) const;
  void deallocate(void* pointer) const noexcept;
  bool classify(const void* pointer, ImageMemoryKind kind) const noexcept;
  bool operator==(const ImageMemoryBudget& other) const noexcept { return state_ == other.state_; }
 private:
  std::shared_ptr<State> state_;
};

template <typename T> class ImageAllocator {
 public:
  using value_type = T;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;
  ImageAllocator() = default;
  explicit ImageAllocator(ImageMemoryKind kind) : kind_(kind) {}
  template <typename U> ImageAllocator(const ImageAllocator<U>& other)
      : budget_(other.budget_), kind_(other.kind_) {}
  T* allocate(std::size_t count) {
    if (count > static_cast<std::size_t>(-1) / sizeof(T)) throw std::bad_array_new_length{};
    return static_cast<T*>(budget_.allocate(count * sizeof(T), kind_));
  }
  void deallocate(T* pointer, std::size_t) noexcept { budget_.deallocate(pointer); }
  ImageAllocator select_on_container_copy_construction() const {
    return ImageAllocator(ImageMemoryKind::WireCopy);
  }
  template <typename U> bool operator==(const ImageAllocator<U>& other) const noexcept {
    return budget_ == other.budget_;
  }
  template <typename U> bool operator!=(const ImageAllocator<U>& other) const noexcept {
    return !(*this == other);
  }
 private:
  template <typename> friend class ImageAllocator;
  ImageMemoryBudget budget_{ImageMemoryBudget::global()};
  ImageMemoryKind kind_{ImageMemoryKind::InFlight};
};
}  // namespace qingying
