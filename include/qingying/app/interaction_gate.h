#pragma once

#include <memory>
#include <stdexcept>
#include <thread>

namespace qingying {

enum class InteractionKind { Capture, LongShot, SaveDialog, Copy, Pin };

// UI-thread ownership, including nested modal message pumps. Only an explicit
// owner may nest; being on the same thread is not permission to reenter.
class InteractionGate final {
  struct Activity { InteractionKind kind; };
  struct State {
    std::thread::id thread{std::this_thread::get_id()};
    bool stopping{false};
    std::weak_ptr<Activity> active;
    void check() const {
      if (thread != std::this_thread::get_id())
        throw std::logic_error("interaction gate requires UI thread");
    }
  };
 public:
  class Guard {
   public:
    Guard() = default;
    Guard(Guard&&) noexcept = default;
    Guard& operator=(Guard&&) noexcept = default;
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    explicit operator bool() const noexcept { return activity_ != nullptr; }
    void reset() noexcept { activity_.reset(); state_.reset(); }
    void setKind(InteractionKind kind) {
      if (state_) { state_->check(); activity_->kind = kind; }
    }
   private:
    friend class InteractionGate;
    Guard(std::shared_ptr<State> state, std::shared_ptr<Activity> activity)
        : state_(std::move(state)), activity_(std::move(activity)) {}
    std::shared_ptr<State> state_;
    std::shared_ptr<Activity> activity_;
  };

  InteractionGate() : state_(std::make_shared<State>()) {}
  InteractionGate(const InteractionGate&) = delete;
  InteractionGate& operator=(const InteractionGate&) = delete;
  Guard acquire(InteractionKind kind, const Guard* owner = nullptr) {
    state_->check();
    if (state_->stopping) return {};
    auto active = state_->active.lock();
    if (owner) {
      if (owner->state_ != state_ || !active || owner->activity_ != active)
        return {};
      return Guard{state_, std::move(active)};
    }
    if (active) return {};
    active = std::make_shared<Activity>(Activity{kind});
    state_->active = active;
    return Guard{state_, std::move(active)};
  }
  bool busy() const { state_->check(); return !state_->active.expired(); }
  bool stopping() const { state_->check(); return state_->stopping; }
  void stop() { state_->check(); state_->stopping = true; }
  const char* reason() const {
    state_->check();
    const auto active = state_->active.lock();
    if (!active) return "";
    switch (active->kind) {
      case InteractionKind::Capture: return "capture";
      case InteractionKind::LongShot: return "longshot";
      case InteractionKind::SaveDialog: return "save_dialog";
      case InteractionKind::Copy: return "copy";
      case InteractionKind::Pin: return "pin";
    }
    return "";
  }
 private:
  std::shared_ptr<State> state_;
};
}  // namespace qingying
