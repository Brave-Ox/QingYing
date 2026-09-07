#pragma once

#include "qingying/action/types.hpp"

#include <memory>
#include <optional>

namespace qingying {

struct ResultBudgetSnapshot {
  std::uint64_t reserved_bytes{0};
  std::uint64_t retained_bytes{0};
  std::uint64_t external_reserved_bytes{0};
  std::uint64_t external_retained_bytes{0};
};

// Shared accounting outlives ResultStore. A reservation rolls back on scope
// exit, or transfers into one shared image allocation at publication.
class ResultBudget {
  struct State;

 public:
  class Reservation {
   public:
    Reservation() = default;
    ~Reservation();
    Reservation(Reservation&& other) noexcept;
    Reservation& operator=(Reservation&& other) noexcept;
    Reservation(const Reservation&) = delete;
    Reservation& operator=(const Reservation&) = delete;
    explicit operator bool() const noexcept { return state_ != nullptr; }
    std::uint64_t bytes() const noexcept { return bytes_; }

   private:
    friend class ResultBudget;
    friend class ResultStore;
    void reset() noexcept;
    void commit() noexcept;
    std::shared_ptr<State> state_;
    std::uint64_t bytes_{0};
    ResultScopeId scope_{kInvalidResultScopeId};
    int width_{0};
    int height_{0};
    bool committed_{false};
  };

  explicit ResultBudget(AutomationLimits limits = {});
  // capacity_bytes includes spare vector capacity, not just its logical size.
  // Ordinary external captures have a pixel limit; longshot output instead
  // uses ordinary_capture=false and remains subject to both byte limits.
  Reservation reserve(ResultScopeId scope, int width, int height,
                      bool ordinary_capture = true,
                      std::uint64_t capacity_bytes = 0);
  ResultBudgetSnapshot snapshot() const noexcept;
  static std::optional<std::uint64_t> imageBytes(int width, int height) noexcept;

 private:
  friend class ResultStore;
  bool owns(const Reservation& reservation) const noexcept;
  std::shared_ptr<State> state_;
};

}  // namespace qingying
