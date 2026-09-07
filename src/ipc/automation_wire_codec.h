#pragma once

#include "qingying/automation/automation_contract.h"

#include <array>
#include <string_view>

namespace qingying::ipc {
constexpr std::uint32_t kWireVersion = 1;
// Integer and string RPC IDs remain distinct. Floats, null and booleans are
// not supported. Positive signed IDs decode to uint64 with the same value.
using RpcId = std::variant<std::int64_t, std::uint64_t, std::string>;
enum class HelloRole { Client, Server };
struct WireHello {
  std::uint32_t wire_version{kWireVersion};
  HelloRole role{HelloRole::Client};
  // Server metadata, NOT proof of identity. Bind only after OS authentication.
  std::optional<ApplicationEpoch> application_epoch;
  std::optional<ConnectionGeneration> connection_generation;
  std::vector<std::string> capabilities;
  std::optional<AutomationLimits> limits;
};
struct WireRequest {
  RpcId rpc_id{std::uint64_t{0}};
  AutomationRequest request;
};
struct WireResponse {
  RpcId rpc_id{std::uint64_t{0}};
  // Connection is deliberately NOT serialized. The authenticated transport
  // supplies it when delivering the decoded response to its bound client.
  AutomationResponse response;
  std::optional<ResultHandle> result_handle;
  std::optional<OperationHandle> operation_handle;
};
enum class EventKind { Progress, Completion };
struct WireEvent {
  EventKind kind{EventKind::Progress};
  OperationSnapshot operation;
  std::optional<ResultHandle> result_handle;
  std::optional<OperationHandle> operation_handle;
};
using WireMessage = std::variant<WireHello, WireRequest, WireResponse, WireEvent>;
enum class WireError {
  None, InvalidConfiguration, InvalidFrameLength, TruncatedFrame, InvalidJson,
  DuplicateKey, DepthLimit, InvalidMessage, UnsupportedVersion, ResourceLimit,
  ConsumerRejected
};
const char* wireErrorSymbol(WireError error) noexcept;
struct DecodedMessage {
  WireError error{WireError::None};
  std::optional<WireMessage> message;
  explicit operator bool() const noexcept { return message.has_value(); }
};
struct EncodedFrame {
  WireError error{WireError::None};
  std::string bytes;
  explicit operator bool() const noexcept { return error == WireError::None; }
};
using WireClock = std::chrono::steady_clock;
// Clock is explicit: monotonic absolute timestamps never cross processes.
DecodedMessage decodeBody(std::string_view body, const AutomationLimits& limits = {},
    WireClock::time_point now = WireClock::now()) noexcept;
EncodedFrame encodeFrame(const WireMessage& message, const AutomationLimits& limits = {},
    WireClock::time_point now = WireClock::now()) noexcept;

// One bounded frame buffered at a time; completed messages are delivered
// immediately so a large batch of glued frames cannot create an output queue.
// Consumer must not reenter this decoder. False/error poisons this connection;
// discard the decoder rather than resynchronizing past malformed input.
class FrameDecoder final {
 public:
  using Consumer = std::function<bool(WireMessage)>;
  explicit FrameDecoder(AutomationLimits limits = {});
  WireError feed(std::string_view bytes, const Consumer& consumer,
                 WireClock::time_point now = WireClock::now()) noexcept;
  WireError finish() noexcept;
  WireError error() const noexcept { return error_; }
  std::size_t bufferedBytes() const noexcept { return header_size_ + body_.size(); }
 private:
  AutomationLimits limits_;
  std::array<unsigned char, 4> header_{};
  std::size_t header_size_{0};
  std::uint32_t length_{0};
  std::string body_;
  WireError error_{WireError::None};
};
}  // namespace qingying::ipc
