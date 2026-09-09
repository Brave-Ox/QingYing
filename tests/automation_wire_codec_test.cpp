#include "automation_wire_codec.h"
#include <gtest/gtest.h>
#include <limits>

namespace qingying::ipc {
namespace {
const auto now = WireClock::time_point{std::chrono::hours{48}};
const std::string status = R"({"type":"execute_action","rpc_id":"rpc-1","request_id":1,"payload":{"action":"status"}})";
std::string framed(std::string body) {
  const auto size = static_cast<std::uint32_t>(body.size());
  std::string result(4, '\0');
  for (unsigned i = 0; i < 4; ++i) result[i] = static_cast<char>((size >> (i * 8)) & 255);
  return result + body;
}
std::string requestWith(std::string payload, std::string type = "execute_action") {
  return "{\"type\":\"" + type + "\",\"rpc_id\":1,\"request_id\":2,\"payload\":" + payload + "}";
}
WireMessage roundtrip(const WireMessage& value) {
  const auto encoded = encodeFrame(value, {}, now);
  EXPECT_EQ(encoded.error, WireError::None);
  if (!encoded) return WireHello{};
  const auto decoded = decodeBody(std::string_view(encoded.bytes).substr(4), {}, now);
  EXPECT_EQ(decoded.error, WireError::None);
  if (!decoded) return WireHello{};
  const auto repeated = encodeFrame(*decoded.message, {}, now);
  EXPECT_EQ(encoded.bytes, repeated.bytes);
  return *decoded.message;
}
WireResponse successful(ActionOutput output = {}) {
  WireResponse result;
  result.rpc_id = std::string("调用-1");
  result.response.result.request_id = 1;
  result.response.result.operation_id = 2;
  result.response.result.ok = true;
  result.response.result.error_code = ErrorCode::kOk;
  result.response.result.output = std::move(output);
  return result;
}
OperationSnapshot running() {
  OperationSnapshot result;
  result.operation_id = 2;
  result.state = OperationState::Running;
  result.submitted_at = now - std::chrono::seconds{2};
  result.updated_at = now;
  result.progress = {"capturing", 3, 1920, 1080, ""};
  return result;
}
TEST(AutomationWireCodecTest, HeaderIsFourByteLittleEndianBodyLength) {
  WireRequest request;
  request.request.request_id = 1;
  const auto frame = encodeFrame(request, {}, now);
  ASSERT_TRUE(frame);
  std::uint32_t size = 0;
  for (unsigned i = 0; i < 4; ++i) size |= std::uint32_t{static_cast<unsigned char>(frame.bytes[i])} << (8 * i);
  EXPECT_EQ(size, frame.bytes.size() - 4);
  EXPECT_EQ(frame.bytes[4], '{');
}
TEST(AutomationWireCodecTest, EverySplitAndBytewiseFeedDecodesExactlyOnce) {
  const auto frame = framed(status);
  for (std::size_t split = 0; split <= frame.size(); ++split) {
    FrameDecoder decoder;
    int calls = 0;
    auto sink = [&](WireMessage value) { ++calls; EXPECT_TRUE(std::holds_alternative<WireRequest>(value)); return true; };
    EXPECT_EQ(decoder.feed(std::string_view(frame).substr(0, split), sink), WireError::None);
    EXPECT_EQ(decoder.feed(std::string_view(frame).substr(split), sink), WireError::None);
    EXPECT_EQ(decoder.finish(), WireError::None);
    EXPECT_EQ(calls, 1);
  }
  FrameDecoder decoder;
  int calls = 0;
  for (const char& byte : frame) EXPECT_EQ(decoder.feed(std::string_view(&byte, 1), [&](WireMessage) { ++calls; return true; }), WireError::None);
  EXPECT_EQ(calls, 1);
}
TEST(AutomationWireCodecTest, GluedFramesStreamWithoutAnAccumulatedMessageQueue) {
  std::string batch;
  for (int i = 0; i < 1000; ++i) batch += framed(status);
  FrameDecoder decoder;
  int calls = 0;
  EXPECT_EQ(decoder.feed(batch, [&](WireMessage) { ++calls; return true; }), WireError::None);
  EXPECT_EQ(calls, 1000);
  EXPECT_EQ(decoder.bufferedBytes(), 0u);
}
TEST(AutomationWireCodecTest, InvalidPrefixIsRejectedBeforeBodyBufferingAndPoisonsDecoder) {
  for (const auto size : {0u, 65537u, UINT32_MAX}) {
    std::string header(4, '\0');
    for (unsigned i = 0; i < 4; ++i) header[i] = static_cast<char>((size >> (8 * i)) & 255);
    FrameDecoder decoder;
    int calls = 0;
    auto sink = [&](WireMessage) { ++calls; return true; };
    EXPECT_EQ(decoder.feed(header, sink), WireError::InvalidFrameLength);
    EXPECT_LE(decoder.bufferedBytes(), 4u);
    EXPECT_EQ(decoder.feed(framed(status), sink), WireError::InvalidFrameLength);
    EXPECT_EQ(calls, 0);
  }
}
TEST(AutomationWireCodecTest, FrameLimitIncludesBodyAndAllowsExactBoundary) {
  auto body = status;
  body.resize(65536, ' ');
  EXPECT_TRUE(decodeBody(body));
  body.push_back(' ');
  EXPECT_EQ(decodeBody(body).error, WireError::InvalidFrameLength);
}
TEST(AutomationWireCodecTest, EofDistinguishesCleanAndTruncatedHeaderOrBody) {
  EXPECT_EQ(FrameDecoder{}.finish(), WireError::None);
  const auto frame = framed(status);
  for (std::size_t length = 1; length < frame.size(); ++length) {
    FrameDecoder decoder;
    ASSERT_EQ(decoder.feed(std::string_view(frame).substr(0, length), [](WireMessage) { return true; }), WireError::None);
    EXPECT_EQ(decoder.finish(), WireError::TruncatedFrame);
  }
}
TEST(AutomationWireCodecTest, ConsumerBackpressureAndExceptionsStopFollowingFrames) {
  FrameDecoder decoder;
  int calls = 0;
  EXPECT_EQ(decoder.feed(framed(status) + framed(status), [&](WireMessage) { ++calls; return false; }), WireError::ConsumerRejected);
  EXPECT_EQ(calls, 1);
  FrameDecoder throwing;
  EXPECT_EQ(throwing.feed(framed(status), [](WireMessage) -> bool { throw std::runtime_error("sink"); }), WireError::ConsumerRejected);
  FrameDecoder allocating;
  EXPECT_EQ(allocating.feed(framed(status), [](WireMessage) -> bool { throw std::bad_alloc{}; }), WireError::ResourceLimit);
}
TEST(AutomationWireCodecTest, DuplicateKeysIncludingEscapedAliasesAreRejected) {
  for (const auto& body : {
      R"({"type":"hello","type":"hello"})",
      R"({"type":"hello","\u0074ype":"hello"})",
      R"({"type":"execute_action","rpc_id":1,"request_id":2,"payload":{"action":"status","action":"copy"}})"}) {
    EXPECT_EQ(decodeBody(body).error, WireError::DuplicateKey);
  }
}
TEST(AutomationWireCodecTest, DepthLimitAbortsBeforeSchemaOrDomConstruction) {
  std::string body(17, '['); body += "0"; body += std::string(17, ']');
  EXPECT_EQ(decodeBody(body).error, WireError::DepthLimit);
  body = std::string(16, '[') + "0" + std::string(16, ']');
  EXPECT_EQ(decodeBody(body).error, WireError::InvalidMessage);
  AutomationLimits limits;
  limits.max_json_depth = 1;
  EXPECT_EQ(decodeBody(status, limits).error, WireError::DepthLimit);
}
TEST(AutomationWireCodecTest, InvalidUtf8EscapesBomAndTrailingInputFail) {
  for (const auto& body : std::vector<std::string>{
      std::string("\xEF\xBB\xBF") + status, status + "{}", "{", "true", "[]",
      requestWith("{\"action\":\"capture_window\",\"query\":\"\xC0\xAF\"}"),
      requestWith(R"({"action":"capture_window","query":"\uD800"})"),
      requestWith(R"({"action":"capture_window","query":"\uDC00"})")}) {
    EXPECT_FALSE(decodeBody(body)) << body;
  }
}
TEST(AutomationWireCodecTest, RpcAndInternalIdsPreserveFullIntegerPrecision) {
  for (const auto& rpc : std::vector<RpcId>{std::uint64_t{UINT64_MAX}, std::int64_t{INT64_MIN},
      std::uint64_t{9007199254740993ULL}, std::string("18446744073709551615")}) {
    WireRequest request;
    request.rpc_id = rpc;
    request.request.request_id = UINT64_MAX;
    request.request.payload = GetOperationRequest{UINT64_MAX - 1};
    const auto decoded = std::get<WireRequest>(roundtrip(request));
    EXPECT_EQ(decoded.rpc_id, rpc);
    EXPECT_EQ(decoded.request.request_id, UINT64_MAX);
    EXPECT_EQ(std::get<GetOperationRequest>(decoded.request.payload).operation_id, UINT64_MAX - 1);
  }
}
TEST(AutomationWireCodecTest, FractionalBooleanNegativeOverflowAndZeroIdsAreRejected) {
  for (const std::string value : {"1.0", "1e0", "true", "null", "-1", "0", "18446744073709551616"}) {
    EXPECT_FALSE(decodeBody("{\"type\":\"get_operation\",\"rpc_id\":1,\"request_id\":" + value +
        ",\"payload\":{\"operation_id\":1}}"));
  }
  EXPECT_FALSE(decodeBody(requestWith(R"({"action":"crop_center","width":2147483648,"height":1})")));
  EXPECT_FALSE(decodeBody(requestWith(R"({"action":"capture_region","region":{"x":-2147483649,"y":0,"width":1,"height":1}})")));
}
TEST(AutomationWireCodecTest, AllActionAndControlRequestsRoundtrip) {
  for (const auto& payload : std::vector<AutomationPayload>{
      ExecuteActionRequest{}, ExecuteActionRequest{CaptureRegionRequest{{-10, 20, 30, 40}}, {}},
      ExecuteActionRequest{CaptureWindowRequest{L"中文窗口", WindowMatchMode::Exact, 42}, {}}, ExecuteActionRequest{CropCenterRequest{1920, 1080}, {}},
      ExecuteActionRequest{CopyRequest{ResultSelection::specific(9)}, "copy-key"},
      ExecuteActionRequest{PinRequest{ResultSelection::specific(9)}, "pin-key"},
      ExecuteActionRequest{SaveRequest{ResultSelection::specific(9), L"D:\\图片\\截图.png"}, "save-key"},
      BeginLongShotRequest{}, GetOperationRequest{9}, CancelOperationRequest{OperationCancellation{9}},
      CancelOperationRequest{RequestCancellation{9}}, ReleaseResultRequest{9}}) {
    WireRequest request;
    request.request.request_id = 1;
    request.request.timeout = std::chrono::milliseconds{1234};
    request.request.payload = payload;
    EXPECT_EQ(std::get<WireRequest>(roundtrip(request)).request.payload.index(), payload.index());
  }
}
TEST(AutomationWireCodecTest, CancellationHasExactlyOneTargetAndCannotCancelItself) {
  for (const auto& payload : {R"({})", R"({"operation_id":1,"request_id":3})", R"({"request_id":2})",
      R"({"request_id":0})", R"({"operation_id":0})"}) EXPECT_FALSE(decodeBody(requestWith(payload, "cancel_operation")));
}
TEST(AutomationWireCodecTest, UnknownFieldsCannotInjectIdentityScopeOrPixels) {
  for (const auto* field : {"pid", "user", "scope", "result_scope", "generation", "application_epoch", "pixels", "image"}) {
    EXPECT_FALSE(decodeBody(requestWith("{\"action\":\"status\",\"" + std::string(field) + "\":1}")));
    auto body = status; body.pop_back(); body += ",\"" + std::string(field) + "\":1}";
    EXPECT_FALSE(decodeBody(body));
  }
  EXPECT_FALSE(decodeBody(requestWith(R"({"action":"arbitrary_input"})")));
  EXPECT_FALSE(decodeBody(requestWith("{}", "unknown_message")));
}
TEST(AutomationWireCodecTest, StringsHonorUtf16UnitsAndFilenameLimits) {
  const auto query = [](std::string text) { return requestWith("{\"action\":\"capture_window\",\"query\":\"" + text + "\"}"); };
  EXPECT_TRUE(decodeBody(query(std::string(1024, 'a'))));
  EXPECT_EQ(decodeBody(query(std::string(1025, 'a'))).error, WireError::ResourceLimit);
  std::string emoji;
  for (int i = 0; i < 512; ++i) emoji += "\xF0\x9F\x98\x80";
  EXPECT_TRUE(decodeBody(query(emoji)));
  EXPECT_EQ(decodeBody(query(emoji + "a")).error, WireError::ResourceLimit);
  EXPECT_EQ(decodeBody(requestWith("{\"action\":\"save\",\"result_id\":1,\"path\":\"" + std::string(256, 'a') + "\"}")).error, WireError::ResourceLimit);
  EXPECT_FALSE(decodeBody(query("a\\u0000b")));
}
TEST(AutomationWireCodecTest, HelloHasStrictVersionAndUntrustedServerMetadata) {
  roundtrip(WireHello{});
  WireHello hello;
  hello.role = HelloRole::Server;
  hello.application_epoch = UINT64_MAX;
  hello.connection_generation = UINT64_MAX - 1;
  hello.capabilities = {"status", "get_operation"};
  hello.limits = AutomationLimits{};
  const auto copy = std::get<WireHello>(roundtrip(hello));
  EXPECT_EQ(copy.application_epoch, UINT64_MAX);
  EXPECT_EQ(copy.connection_generation, UINT64_MAX - 1);
  EXPECT_EQ(copy.limits->max_frame_bytes, 65536u);
  EXPECT_EQ(decodeBody(R"({"type":"hello","wire_version":2,"role":"client","capabilities":[]})").error, WireError::UnsupportedVersion);
  EXPECT_FALSE(decodeBody(R"({"type":"hello","wire_version":1,"role":"client","capabilities":[],"application_epoch":1})"));
  EXPECT_FALSE(decodeBody(R"({"type":"hello","wire_version":1,"role":"server","capabilities":[]})"));
  EXPECT_FALSE(decodeBody(R"({"type":"hello","wire_version":1,"role":"client","capabilities":["status","status"]})"));
}
TEST(AutomationWireCodecTest, TypedOutputsAndOpaqueHandlesRoundtripWithoutConnectionIdentity) {
  StatusInfo info;
  info.reachable = true; info.busy = false; info.capabilities = {"status"};
  info.limits = AutomationLimits{};
  info.resources = ResourceUsage{10, 1, 20, 30};
  info.queues = QueueUsage{1, 2, 2, 1};
  for (const auto& output : std::vector<ActionOutput>{std::monostate{}, info,
      CapturedResult{9, 1, 1, {0, 0, 1, 1}, CaptureMode::VisibleScreen, now + std::chrono::seconds{60}},
      SavedResult{9, L"D:\\截图.png"}, CopiedResult{9}, PinnedResult{9, UINT64_MAX},
      WindowCandidates{{{L"窗口", 123, {0, 0, 1, 1}, "opaque-window"}}, true}}) {
    auto response = successful(output);
    response.result_handle = ResultHandle{"opaque-result-中文"};
    response.operation_handle = OperationHandle{"opaque-operation-18446744073709551615"};
    response.response.connection = {123, 456};
    const auto copy = std::get<WireResponse>(roundtrip(response));
    EXPECT_EQ(copy.response.result.output.index(), output.index());
    EXPECT_EQ(copy.result_handle->value, response.result_handle->value);
    EXPECT_EQ(copy.operation_handle->value, response.operation_handle->value);
    EXPECT_FALSE(copy.response.connection.valid());
  }
}
TEST(AutomationWireCodecTest, OperationProgressCompletionAndRelativeClocksRoundtrip) {
  WireEvent event;
  event.operation = running();
  roundtrip(event);
  event.kind = EventKind::Completion;
  event.operation.state = OperationState::Succeeded;
  event.operation.completed_at = now;
  event.operation.outcome = successful().response.result;
  const auto encoded = encodeFrame(event, {}, now);
  ASSERT_TRUE(encoded);
  const auto later = now + std::chrono::hours{3};
  const auto decoded = decodeBody(std::string_view(encoded.bytes).substr(4), {}, later);
  ASSERT_TRUE(decoded);
  const auto copy = std::get<WireEvent>(*decoded.message);
  EXPECT_EQ(copy.operation.submitted_at, later - std::chrono::seconds{2});
  EXPECT_EQ(copy.operation.completed_at, later);
  roundtrip(event);
}
TEST(AutomationWireCodecTest, ControlResultsRoundtripAndFailedOperationQueryRemainsSuccessful) {
  auto response = successful();
  auto snapshot = running();
  snapshot.state = OperationState::Failed; snapshot.completed_at = now;
  snapshot.outcome = successful().response.result;
  snapshot.outcome->ok = false; snapshot.outcome->error_code = ErrorCode::kCaptureFailed;
  response.response.control = snapshot;
  EXPECT_TRUE(std::get<WireResponse>(roundtrip(response)).response.result.ok);
  response.response.control = CancellationResult{OperationCancellation{2}, 2, OperationState::Cancelling, true};
  roundtrip(response);
  response.response.result.operation_id = 0;
  response.response.control = ReleasedResult{9, true};
  roundtrip(response);
}
TEST(AutomationWireCodecTest, ProducerValidationRejectsInvalidEnumsTerminalStatesAndUtf16) {
  auto response = successful(CapturedResult{9, 1, 1});
  std::get<CapturedResult>(response.response.result.output).capture_mode = static_cast<CaptureMode>(99);
  EXPECT_FALSE(encodeFrame(response, {}, now));
  WireEvent event;
  event.operation = running(); event.operation.state = static_cast<OperationState>(99);
  EXPECT_FALSE(encodeFrame(event, {}, now));
  event.operation = running(); event.kind = EventKind::Completion;
  EXPECT_FALSE(encodeFrame(event, {}, now));
  WireRequest request;
  request.request.request_id = 1;
  request.request.payload = ExecuteActionRequest{CaptureWindowRequest{std::wstring(1, wchar_t(0xD800))}, {}};
  EXPECT_FALSE(encodeFrame(request, {}, now));
}
TEST(AutomationWireCodecTest, EncoderBoundsTotalSerializedSizeIncludingEscapes) {
  auto response = successful();
  response.response.result.message = std::string(20000, '\n');
  response.response.result.data = std::string(20000, '\n');
  EXPECT_EQ(encodeFrame(response, {}, now).error, WireError::InvalidFrameLength);
}
TEST(AutomationWireCodecTest, InvalidConfigurationCannotRelaxHardWireLimits) {
  AutomationLimits limits;
  limits.max_frame_bytes = 65537;
  EXPECT_EQ(decodeBody(status, limits).error, WireError::InvalidConfiguration);
  EXPECT_EQ(FrameDecoder(limits).error(), WireError::InvalidConfiguration);
  limits = {}; limits.max_json_depth = 17;
  EXPECT_EQ(encodeFrame(WireHello{}, limits).error, WireError::InvalidConfiguration);
}
TEST(AutomationWireCodecTest, MalformedFrameStopsBeforeLaterValidFrames) {
  FrameDecoder decoder;
  int calls = 0;
  const auto batch = framed(status) + framed("{") + framed(status);
  EXPECT_EQ(decoder.feed(batch, [&](WireMessage) { ++calls; return true; }), WireError::InvalidJson);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(decoder.finish(), WireError::InvalidJson);
}
TEST(AutomationWireCodecTest, TimeoutPreservesAbsenceAndRejectsInvalidDurations) {
  const auto defaulted = decodeBody(status);
  ASSERT_TRUE(defaulted);
  EXPECT_FALSE(std::get<WireRequest>(*defaulted.message).request.timeout);
  for (const std::string timeout : {"0", "-1", "null", "true", "1800001", "1.5"}) {
    auto body = status; body.pop_back(); body += ",\"timeout_ms\":" + timeout + "}";
    EXPECT_FALSE(decodeBody(body));
  }
}
TEST(AutomationWireCodecTest, HandlesAndKeysAreBoundedAndNeverDecodedAsIntegers) {
  auto response = successful();
  response.result_handle = ResultHandle{std::string(128, 'r')};
  EXPECT_TRUE(encodeFrame(response, {}, now));
  response.result_handle->value += 'r';
  EXPECT_EQ(encodeFrame(response, {}, now).error, WireError::ResourceLimit);
  response.result_handle->value = std::string("a\0b", 3);
  EXPECT_FALSE(encodeFrame(response, {}, now));
  response.result_handle->value = "18446744073709551615";
  auto frame = encodeFrame(response, {}, now);
  ASSERT_TRUE(frame);
  auto body = frame.bytes.substr(4);
  const std::string quoted = "\"18446744073709551615\"";
  const auto position = body.find(quoted);
  ASSERT_NE(position, std::string::npos);
  body.replace(position, quoted.size(), "18446744073709551615");
  EXPECT_FALSE(decodeBody(body, {}, now));
  EXPECT_EQ(decodeBody(requestWith("{\"action\":\"copy\",\"result_id\":1,\"request_key\":\"" +
      std::string(129, 'k') + "\"}")).error, WireError::ResourceLimit);
}
TEST(AutomationWireCodecTest, CopyOpaqueResultAndRequestKeyRoundtrip) {
  WireRequest wire;
  wire.request.request_id = 77;
  ExecuteActionRequest execute;
  execute.payload = CopyRequest{ResultSelection::current()};
  execute.result_handle = ResultHandle{"result_owned"};
  execute.request_key = "copy-once";
  wire.request.payload = execute;
  const auto encoded = encodeFrame(wire);
  ASSERT_TRUE(encoded);
  const auto decoded = decodeBody(std::string_view(encoded.bytes).substr(4));
  ASSERT_TRUE(decoded);
  const auto& decoded_execute = std::get<ExecuteActionRequest>(
      std::get<WireRequest>(*decoded.message).request.payload);
  EXPECT_EQ(std::get<CopyRequest>(decoded_execute.payload).result.kind,
            ResultSelectionKind::Current);
  ASSERT_TRUE(decoded_execute.result_handle);
  EXPECT_EQ(decoded_execute.result_handle->value, "result_owned");
  EXPECT_EQ(decoded_execute.request_key, "copy-once");
}
TEST(AutomationWireCodecTest, PinOpaqueResultAndRequestKeyRoundtrip) {
  WireRequest wire;
  wire.request.request_id = 78;
  ExecuteActionRequest execute;
  execute.payload = PinRequest{ResultSelection::current()};
  execute.result_handle = ResultHandle{"result_owned"};
  execute.request_key = "pin-once";
  wire.request.payload = execute;
  const auto encoded = encodeFrame(wire);
  ASSERT_TRUE(encoded);
  const auto decoded = decodeBody(std::string_view(encoded.bytes).substr(4));
  ASSERT_TRUE(decoded);
  const auto& decoded_execute = std::get<ExecuteActionRequest>(
      std::get<WireRequest>(*decoded.message).request.payload);
  EXPECT_EQ(std::get<PinRequest>(decoded_execute.payload).result.kind,
            ResultSelectionKind::Current);
  ASSERT_TRUE(decoded_execute.result_handle);
  EXPECT_EQ(decoded_execute.result_handle->value, "result_owned");
  EXPECT_EQ(decoded_execute.request_key, "pin-once");
}
TEST(AutomationWireCodecTest, RectangleArithmeticAndRelativeTimeCannotOverflow) {
  EXPECT_FALSE(decodeBody(requestWith(R"({"action":"capture_region","region":{"x":2147483647,"y":0,"width":1,"height":1}})")));
  EXPECT_FALSE(decodeBody(status, {}, (WireClock::time_point::max)()));
  EXPECT_FALSE(encodeFrame(WireHello{}, {}, (WireClock::time_point::min)()));
}
TEST(AutomationWireCodecTest, CorruptedStateAndUnknownResponseFieldsAreRejected) {
  WireEvent event;
  event.operation = running();
  auto frame = encodeFrame(event, {}, now);
  ASSERT_TRUE(frame);
  auto body = frame.bytes.substr(4);
  const auto position = body.find("\"running\"");
  ASSERT_NE(position, std::string::npos);
  body.replace(position, 9, "\"unknown\"");
  EXPECT_FALSE(decodeBody(body, {}, now));
  frame = encodeFrame(successful(), {}, now);
  ASSERT_TRUE(frame);
  body = frame.bytes.substr(4); body.pop_back(); body += ",\"generation\":1}";
  EXPECT_FALSE(decodeBody(body, {}, now));
}
TEST(AutomationWireCodecTest, PartialSuffixAfterCompleteFramesIsReportedAtEof) {
  FrameDecoder decoder;
  int calls = 0;
  const auto frame = framed(status);
  EXPECT_EQ(decoder.feed(frame + frame.substr(0, 7), [&](WireMessage) { ++calls; return true; }), WireError::None);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(decoder.finish(), WireError::TruncatedFrame);
}
}  // namespace
}  // namespace qingying::ipc
