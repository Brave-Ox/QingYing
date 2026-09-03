#pragma once

#include "qingying/app/app_messages.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace qingying {

// Owns payloads independently of the Windows message queue. A worker stores a
// typed event here and only passes the returned token through PostMessage.
// The UI thread consumes or drains the event; no WndProc owns a payload.
class UiMessageChannel final {
 public:
  UiMessageChannel() = default;
  ~UiMessageChannel() { drain(); }

  UiMessageChannel(const UiMessageChannel&) = delete;
  UiMessageChannel& operator=(const UiMessageChannel&) = delete;

  template <typename Payload>
  std::optional<UiMessageToken> push(Payload payload) noexcept {
    try {
      auto message =
          std::make_unique<TypedMessage<Payload>>(std::move(payload));
      std::lock_guard<std::mutex> lock(mutex_);
      const UiMessageToken token = allocateTokenLocked();
      messages_.emplace(token, std::move(message));
      return token;
    } catch (...) {
      return std::nullopt;
    }
  }

  template <typename Payload>
  std::optional<Payload> take(UiMessageToken token) {
    if (token == kInvalidUiMessageToken) {
      return std::nullopt;
    }

    std::unique_ptr<Message> message;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto it = messages_.find(token);
      if (it == messages_.end()) {
        return std::nullopt;
      }
      message = std::move(it->second);
      messages_.erase(it);
    }

    auto* typed = dynamic_cast<TypedMessage<Payload>*>(message.get());
    if (typed == nullptr) {
      return std::nullopt;
    }
    return std::move(typed->payload);
  }

  bool discard(UiMessageToken token) noexcept {
    if (token == kInvalidUiMessageToken) {
      return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_.erase(token) != 0;
  }

  void drain() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.clear();
  }

  std::size_t size() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_.size();
  }

 private:
  struct Message {
    virtual ~Message() = default;
  };

  template <typename Payload>
  struct TypedMessage final : Message {
    explicit TypedMessage(Payload value) : payload(std::move(value)) {}

    Payload payload;
  };

  UiMessageToken allocateTokenLocked() noexcept {
    for (;;) {
      const UiMessageToken token = next_token_;
      ++next_token_;
      if (next_token_ == kInvalidUiMessageToken) {
        next_token_ = 1;
      }
      if (token != kInvalidUiMessageToken &&
          messages_.find(token) == messages_.end()) {
        return token;
      }
    }
  }

  mutable std::mutex mutex_;
  std::map<UiMessageToken, std::unique_ptr<Message>> messages_;
  UiMessageToken next_token_{1};
};

}  // namespace qingying
