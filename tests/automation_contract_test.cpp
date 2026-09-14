#include "qingying/automation/automation_contract.h"

#include "qingying/action/compatibility/legacy_action_dispatcher.h"

#ifdef _WINDOWS_
#error "The automation contract must not include Windows headers."
#endif

#include <gtest/gtest.h>

#include <array>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

using namespace qingying;
using namespace std::chrono_literals;

static_assert(std::variant_size_v<ActionPayload> == 7,
              "F9-01 must preserve the existing action payload alternatives");
static_assert(!std::is_same_v<ResultHandle, OperationHandle>);
static_assert(!std::is_convertible_v<ResultHandle, OperationHandle>);
static_assert(!std::is_convertible_v<OperationHandle, ResultHandle>);
static_assert(!std::is_constructible_v<ResultHandle, ResultId>);
static_assert(!std::is_constructible_v<OperationHandle, OperationId>);
static_assert(std::variant_size_v<CancellationTarget> == 2);
static_assert(!std::is_constructible_v<CancellationTarget, OperationId>);
static_assert(std::is_abstract_v<IAutomationClient>);
static_assert(std::has_virtual_destructor_v<IAutomationClient>);
static_assert(noexcept(std::declval<IAutomationClient&>().close()));

AutomationRequest requestWith(AutomationPayload payload) {
  AutomationRequest request;
  request.request_id = 17;
  request.payload = std::move(payload);
  return request;
}

std::array<ActionPayload, 3> consumers(ResultSelection selection) {
  return {SaveRequest{selection, L"capture.png"}, CopyRequest{selection},
          PinRequest{selection}};
}

TEST(AutomationContractTest, ExistingActionDefaultsToGuiScopeAndEmptyOutput) {
  const ActionRequest request = makeActionRequest(StatusRequest{});
  EXPECT_EQ(request.context.result_scope, kGuiResultScopeId);
  EXPECT_TRUE(request.context.valid());
  EXPECT_EQ(request.request_id, kInvalidRequestId);
  EXPECT_EQ(request.operation_id, kInvalidOperationId);
  EXPECT_TRUE(validateActionRequest(request).valid);

  const ActionResult result;
  EXPECT_TRUE(std::holds_alternative<std::monostate>(result.output));
  EXPECT_EQ(PinnedResult{}.pin_id, kInvalidPinId);
  EXPECT_FALSE(CapturedResult{}.expires_at.has_value());
}

TEST(AutomationContractTest, ExistingTypedPayloadsKeepTheirConstructionAndType) {
  const std::array<ActionPayload, 7> payloads{
      StatusRequest{},
      CaptureRegionRequest{ScreenPhysicalRect{-100, 10, 640, 480}},
      CaptureWindowRequest{L"Editor"}, CropCenterRequest{640, 480},
      CopyRequest{}, SaveRequest{ResultSelection::current(), L"capture.png"},
      PinRequest{}};
  const std::array<ActionType, 7> types{
      ActionType::Status, ActionType::CaptureRegion, ActionType::CaptureWindow,
      ActionType::CropCenter, ActionType::Copy, ActionType::Save, ActionType::Pin};
  for (std::size_t index = 0; index < payloads.size(); ++index) {
    SCOPED_TRACE(index);
    const ActionRequest request{payloads[index]};
    EXPECT_EQ(request.type(), types[index]);
    EXPECT_EQ(request.context.result_scope, kGuiResultScopeId);
    EXPECT_TRUE(validateActionRequest(request).valid);
  }
}

TEST(AutomationContractTest, LegacySaveStillAdaptsToGuiCurrentResult) {
  LegacyActionRequest legacy;
  legacy.type = ActionType::Save;
  legacy.save_path = L"old-caller.png";
  const auto request = adaptLegacyActionRequest(legacy);
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(request->context.result_scope, kGuiResultScopeId);
  const auto* save = std::get_if<SaveRequest>(&request->payload);
  ASSERT_NE(save, nullptr);
  EXPECT_EQ(save->path, legacy.save_path);
  EXPECT_EQ(save->result.kind, ResultSelectionKind::Current);
  EXPECT_TRUE(validateActionRequest(*request).valid);

  const ActionResult result{11, 22, true, ErrorCode::kOk, "ok", "old-data",
                            "", 0};
  EXPECT_EQ(result.data, "old-data");
  EXPECT_TRUE(std::holds_alternative<std::monostate>(result.output));
}

TEST(AutomationContractTest, ScopeAndConnectionRequireNonzeroIdentity) {
  EXPECT_FALSE((ActionContext{kInvalidResultScopeId}.valid()));
  EXPECT_TRUE((ActionContext{kGuiResultScopeId}.valid()));
  EXPECT_TRUE((ActionContext{2}.valid()));
  EXPECT_FALSE(AutomationConnection{}.valid());
  EXPECT_FALSE((AutomationConnection{1, 0}.valid()));
  EXPECT_FALSE((AutomationConnection{0, 1}.valid()));
  EXPECT_TRUE((AutomationConnection{1, 1}.valid()));
  ActionRequest request = makeActionRequest(StatusRequest{});
  request.context.result_scope = kInvalidResultScopeId;
  EXPECT_FALSE(validateActionRequest(request).valid);
}

TEST(AutomationContractTest, ConnectionIdentityIncludesBothEpochAndGeneration) {
  const AutomationConnection connection{31, 5};
  EXPECT_TRUE(sameConnection(connection, AutomationConnection{31, 5}));
  EXPECT_FALSE(sameConnection(connection, AutomationConnection{32, 5}));
  EXPECT_FALSE(sameConnection(connection, AutomationConnection{31, 6}));
  EXPECT_FALSE(sameConnection(AutomationConnection{}, AutomationConnection{}));
  EXPECT_FALSE(sameConnection(connection, AutomationConnection{}));
}

TEST(AutomationContractTest, TrustedContextRejectsGuiAndInvalidScopes) {
  TrustedAutomationContext context;
  EXPECT_FALSE(context.valid());
  context.connection = AutomationConnection{31, 5};
  EXPECT_FALSE(context.valid());
  context.action.result_scope = kInvalidResultScopeId;
  EXPECT_FALSE(context.valid());
  context.action.result_scope = 2;
  EXPECT_TRUE(context.valid());
  context.connection.generation = kInvalidConnectionGeneration;
  EXPECT_FALSE(context.valid());
}

TEST(AutomationContractTest, TrustedContextCopyPreservesAdmissionAndCancellation) {
  CancellationSource cancellation;
  TrustedAutomationContext admitted;
  admitted.connection = AutomationConnection{31, 5};
  admitted.action.result_scope = 2;
  admitted.cancellation = cancellation.token();
  admitted.submitted_at = std::chrono::steady_clock::now() - 3s;
  const TrustedAutomationContext requeued = admitted;
  EXPECT_EQ(requeued.submitted_at, admitted.submitted_at);
  EXPECT_TRUE(sameConnection(requeued.connection, admitted.connection));
  EXPECT_FALSE(requeued.cancellation.isCancellationRequested());
  cancellation.cancel();
  EXPECT_TRUE(requeued.cancellation.isCancellationRequested());
}

TEST(AutomationContractTest, OpaqueHandlesRejectEmptyAndEmbeddedNulValues) {
  EXPECT_FALSE(ResultHandle{}.valid());
  EXPECT_FALSE(OperationHandle{}.valid());
  EXPECT_TRUE(ResultHandle{"opaque-result-token"}.valid());
  EXPECT_TRUE(OperationHandle{"opaque-operation-token"}.valid());
  const std::string embedded_nul{"token\0suffix", 12};
  EXPECT_FALSE(ResultHandle{embedded_nul}.valid());
  EXPECT_FALSE(OperationHandle{embedded_nul}.valid());
}

TEST(AutomationContractTest, EveryAutomationRequestRequiresInvocationId) {
  const std::array<AutomationPayload, 5> payloads{
      ExecuteActionRequest{}, BeginLongShotRequest{}, GetOperationRequest{9},
      CancelOperationRequest{OperationCancellation{9}}, ReleaseResultRequest{9}};
  for (const auto& payload : payloads) {
    SCOPED_TRACE(payload.index());
    AutomationRequest request = requestWith(payload);
    EXPECT_TRUE(validateAutomationRequest(request).valid);
    request.request_id = kInvalidRequestId;
    EXPECT_FALSE(validateAutomationRequest(request).valid);
  }
}

TEST(AutomationContractTest, ExecuteCanBeAdmittedBeforeOperationAllocation) {
  const AutomationRequest request = requestWith(ExecuteActionRequest{
      CaptureRegionRequest{ScreenPhysicalRect{-50, 10, 640, 480}}, std::nullopt});
  ASSERT_TRUE(validateAutomationRequest(request).valid);
  const auto& execute = std::get<ExecuteActionRequest>(request.payload);
  const ActionRequest action{execute.payload};
  EXPECT_EQ(action.request_id, kInvalidRequestId);
  EXPECT_EQ(action.operation_id, kInvalidOperationId);
  EXPECT_TRUE(validateActionRequest(action).valid);
}

TEST(AutomationContractTest, AutomationConsumptionRequiresExplicitResult) {
  for (const auto& payload : consumers(ResultSelection::current())) {
    SCOPED_TRACE(payload.index());
    EXPECT_TRUE(validateActionRequest(ActionRequest{payload}).valid);
    EXPECT_FALSE(validateAutomationRequest(requestWith(
        ExecuteActionRequest{payload, std::nullopt})).valid);
  }
  for (const auto& payload : consumers(ResultSelection::specific(9))) {
    SCOPED_TRACE(payload.index());
    EXPECT_TRUE(validateAutomationRequest(requestWith(
        ExecuteActionRequest{payload, std::nullopt})).valid);
  }
}

TEST(AutomationContractTest, InvalidResultSelectionCombinationsAreRejected) {
  const std::array<ResultSelection, 3> selections{
      ResultSelection::specific(kInvalidResultId),
      ResultSelection{ResultSelectionKind::Current, 9},
      ResultSelection{static_cast<ResultSelectionKind>(99), 9}};
  for (const auto selection : selections) {
    SCOPED_TRACE(static_cast<int>(selection.kind));
    EXPECT_FALSE(selection.valid());
    for (const auto& payload : consumers(selection)) {
      SCOPED_TRACE(payload.index());
      EXPECT_FALSE(validateAutomationRequest(requestWith(
          ExecuteActionRequest{payload, std::nullopt})).valid);
    }
  }
}

TEST(AutomationContractTest, ExecuteRetainsExistingPayloadValidation) {
  const std::array<ActionPayload, 4> payloads{
      CaptureRegionRequest{}, CaptureWindowRequest{}, CropCenterRequest{},
      SaveRequest{ResultSelection::specific(9), L""}};
  for (const auto& payload : payloads) {
    SCOPED_TRACE(payload.index());
    const auto validation = validateAutomationRequest(requestWith(
        ExecuteActionRequest{payload, std::nullopt}));
    EXPECT_FALSE(validation.valid);
    EXPECT_FALSE(validation.message.empty());
  }
}

TEST(AutomationContractTest, RequestKeyOnlyAppliesToResultConsumers) {
  const std::array<ActionPayload, 4> producers{
      StatusRequest{}, CaptureRegionRequest{ScreenPhysicalRect{0, 0, 64, 64}},
      CaptureWindowRequest{L"Editor"}, CropCenterRequest{64, 64}};
  for (const auto& payload : producers) {
    SCOPED_TRACE(payload.index());
    EXPECT_FALSE(validateAutomationRequest(requestWith(
        ExecuteActionRequest{payload, std::string{"request-key"}})).valid);
  }
  for (const auto& payload : consumers(ResultSelection::specific(9))) {
    SCOPED_TRACE(payload.index());
    EXPECT_TRUE(validateAutomationRequest(requestWith(
        ExecuteActionRequest{payload, std::string{"request-key"}})).valid);
    EXPECT_FALSE(validateAutomationRequest(requestWith(
        ExecuteActionRequest{payload, std::string{}})).valid);
    EXPECT_FALSE(validateAutomationRequest(requestWith(ExecuteActionRequest{
        payload, std::string{"key\0suffix", 10}})).valid);
  }
}

TEST(AutomationContractTest, GetCancelAndReleaseRejectZeroTargetIds) {
  EXPECT_FALSE(validateAutomationRequest(requestWith(GetOperationRequest{})).valid);
  EXPECT_FALSE(validateAutomationRequest(requestWith(CancelOperationRequest{})).valid);
  EXPECT_FALSE(validateAutomationRequest(requestWith(ReleaseResultRequest{})).valid);
  EXPECT_TRUE(validateAutomationRequest(requestWith(GetOperationRequest{9})).valid);
  EXPECT_TRUE(validateAutomationRequest(requestWith(
      CancelOperationRequest{OperationCancellation{9}})).valid);
  EXPECT_TRUE(validateAutomationRequest(requestWith(ReleaseResultRequest{9})).valid);
}

TEST(AutomationContractTest, CancellationTargetsAreExclusiveAndAllowRequestIds) {
  CancelOperationRequest cancel{OperationCancellation{9}};
  EXPECT_TRUE(std::holds_alternative<OperationCancellation>(cancel.target));
  cancel.target = RequestCancellation{11};
  EXPECT_FALSE(std::holds_alternative<OperationCancellation>(cancel.target));
  EXPECT_TRUE(std::holds_alternative<RequestCancellation>(cancel.target));
  EXPECT_TRUE(validateAutomationRequest(requestWith(cancel)).valid);

  cancel.target = RequestCancellation{kInvalidRequestId};
  EXPECT_FALSE(validateAutomationRequest(requestWith(cancel)).valid);
  cancel.target = RequestCancellation{17};
  EXPECT_FALSE(validateAutomationRequest(requestWith(cancel)).valid);

  CancellationResult response;
  response.target = RequestCancellation{11};
  response.cancellation_requested = true;
  EXPECT_EQ(response.operation_id, kInvalidOperationId);
  EXPECT_EQ(std::get<RequestCancellation>(response.target).request_id, 11u);
}

TEST(AutomationContractTest, TimeoutDefaultsDifferFromExplicitNonpositiveValues) {
  const std::array<AutomationPayload, 5> payloads{
      ExecuteActionRequest{}, BeginLongShotRequest{}, GetOperationRequest{9},
      CancelOperationRequest{OperationCancellation{9}}, ReleaseResultRequest{9}};
  for (const auto& payload : payloads) {
    SCOPED_TRACE(payload.index());
    AutomationRequest request = requestWith(payload);
    EXPECT_FALSE(request.timeout.has_value());
    EXPECT_TRUE(validateAutomationRequest(request).valid);
    request.timeout = 0ms;
    EXPECT_FALSE(validateAutomationRequest(request).valid);
    request.timeout = -1ms;
    EXPECT_FALSE(validateAutomationRequest(request).valid);
    request.timeout = 1ms;
    EXPECT_TRUE(validateAutomationRequest(request).valid);
  }
}

TEST(AutomationContractTest, DefaultLimitsMatchInitialAutomationProfile) {
  constexpr AutomationLimits limits;
  static_assert(limits.valid());
  EXPECT_EQ(limits.max_connections, 4u);
  EXPECT_EQ(limits.max_results_per_scope, 1u);
  EXPECT_EQ(limits.result_ttl, 60s);
  EXPECT_EQ(limits.max_capture_pixels, 16777216u);
  EXPECT_EQ(limits.max_result_bytes, 128ULL * 1024 * 1024);
  EXPECT_EQ(limits.max_retained_result_bytes, 128ULL * 1024 * 1024);
  EXPECT_EQ(limits.max_desktop_operations, 1u);
  EXPECT_EQ(limits.max_queued_requests_per_connection, 8u);
  EXPECT_EQ(limits.max_queued_requests_global, 32u);
  EXPECT_EQ(limits.max_completed_operations_per_connection, 64u);
  EXPECT_EQ(limits.completed_operation_ttl, 5min);
  EXPECT_EQ(limits.max_agent_pins, 8u);
  EXPECT_EQ(limits.max_agent_pin_bytes, 64ULL * 1024 * 1024);
  EXPECT_EQ(limits.max_frame_bytes, 64ULL * 1024);
  EXPECT_EQ(limits.max_json_depth, 16u);
  EXPECT_EQ(limits.default_longshot_timeout, 180s);
  EXPECT_EQ(limits.default_request_timeout, 30s);
  EXPECT_EQ(limits.max_request_timeout, 30min);
}

TEST(AutomationContractTest, TimeoutUsesPerActionDefaultsAndInjectedPolicy) {
  AutomationLimits limits;
  AutomationRequest execute = requestWith(ExecuteActionRequest{});
  AutomationRequest longshot = requestWith(BeginLongShotRequest{});
  EXPECT_EQ(automationTimeout(execute, limits), 30s);
  EXPECT_EQ(automationTimeout(longshot, limits), 180s);
  limits.default_request_timeout = 2s;
  limits.default_longshot_timeout = 4s;
  limits.max_request_timeout = 5s;
  EXPECT_TRUE(limits.valid());
  EXPECT_EQ(automationTimeout(execute, limits), 2s);
  EXPECT_EQ(automationTimeout(longshot, limits), 4s);
  execute.timeout = 5s;
  longshot.timeout = 1s;
  EXPECT_EQ(automationTimeout(execute, limits), 5s);
  EXPECT_EQ(automationTimeout(longshot, limits), 1s);
  EXPECT_TRUE(validateAutomationRequest(execute, limits).valid);
  execute.timeout = 5001ms;
  EXPECT_FALSE(validateAutomationRequest(execute, limits).valid);
  execute.timeout = (std::chrono::milliseconds::max)();
  EXPECT_FALSE(validateAutomationRequest(execute, limits).valid);
  execute.timeout.reset();
  limits.max_connections = 0;
  EXPECT_FALSE(validateAutomationRequest(execute, limits).valid);
}

TEST(AutomationContractTest, LimitsCanBeInjectedWithSmallerResourceBudgets) {
  AutomationLimits limits;
  limits.max_connections = 1;
  limits.max_capture_pixels = 1024;
  limits.max_result_bytes = 4096;
  limits.max_retained_result_bytes = 8192;
  limits.max_queued_requests_per_connection = 1;
  limits.max_queued_requests_global = 2;
  limits.max_control_requests_per_connection = 1;
  limits.max_control_requests_global = 1;
  limits.result_ttl = 1s;
  limits.default_longshot_timeout = 5s;
  EXPECT_TRUE(limits.valid());

  StatusInfo status;
  status.limits = limits;
  ASSERT_TRUE(status.limits.has_value());
  EXPECT_EQ(status.limits->max_result_bytes, 4096u);
  EXPECT_TRUE(status.limits->valid());
}

TEST(AutomationContractTest, EveryCountAndByteLimitMustBePositive) {
  std::uint32_t AutomationLimits::* const counts[]{
      &AutomationLimits::max_connections,
      &AutomationLimits::max_results_per_scope,
      &AutomationLimits::max_desktop_operations,
      &AutomationLimits::max_queued_requests_per_connection,
      &AutomationLimits::max_queued_requests_global,
      &AutomationLimits::max_completed_operations_per_connection,
      &AutomationLimits::max_agent_pins,
      &AutomationLimits::max_json_depth,
      &AutomationLimits::max_control_requests_per_connection,
      &AutomationLimits::max_control_requests_global,
      &AutomationLimits::max_output_frames_per_connection,
      &AutomationLimits::max_export_workers,
      &AutomationLimits::max_queued_exports,
      &AutomationLimits::max_window_query_utf16_units,
      &AutomationLimits::max_filename_utf16_units,
      &AutomationLimits::max_path_utf16_units,
      &AutomationLimits::max_request_key_bytes,
      &AutomationLimits::max_opaque_handle_bytes,
      &AutomationLimits::max_tombstones_per_connection};
  for (std::size_t index = 0; index < std::size(counts); ++index) {
    SCOPED_TRACE(index);
    AutomationLimits limits;
    limits.*counts[index] = 0;
    EXPECT_FALSE(limits.valid());
  }
  std::uint64_t AutomationLimits::* const bytes[]{
      &AutomationLimits::max_capture_pixels, &AutomationLimits::max_result_bytes,
      &AutomationLimits::max_retained_result_bytes,
      &AutomationLimits::max_agent_pin_bytes, &AutomationLimits::max_frame_bytes,
      &AutomationLimits::max_output_bytes_per_connection};
  for (std::size_t index = 0; index < std::size(bytes); ++index) {
    SCOPED_TRACE(index);
    AutomationLimits limits;
    limits.*bytes[index] = 0;
    EXPECT_FALSE(limits.valid());
  }
}

TEST(AutomationContractTest, EveryRetentionAndDefaultTimeoutMustBePositive) {
  std::chrono::milliseconds AutomationLimits::* const durations[]{
      &AutomationLimits::result_ttl,
      &AutomationLimits::completed_operation_ttl,
      &AutomationLimits::default_longshot_timeout,
      &AutomationLimits::default_request_timeout,
      &AutomationLimits::max_request_timeout,
      &AutomationLimits::tombstone_ttl};
  for (std::size_t index = 0; index < std::size(durations); ++index) {
    SCOPED_TRACE(index);
    AutomationLimits limits;
    limits.*durations[index] = 0ms;
    EXPECT_FALSE(limits.valid());
    limits.*durations[index] = -1ms;
    EXPECT_FALSE(limits.valid());
  }
}

TEST(AutomationContractTest, LimitsRejectInconsistentResourceBudgets) {
  using Mutation = void (*)(AutomationLimits&);
  const Mutation mutations[]{
      [](AutomationLimits& limits) {
        limits.max_result_bytes = limits.max_retained_result_bytes + 1;
      },
      [](AutomationLimits& limits) {
        limits.max_capture_pixels = limits.max_result_bytes / 4 + 1;
      },
      [](AutomationLimits& limits) {
        limits.max_queued_requests_per_connection =
            limits.max_queued_requests_global + 1;
      },
      [](AutomationLimits& limits) {
        limits.max_control_requests_per_connection =
            limits.max_control_requests_global + 1;
      },
      [](AutomationLimits& limits) {
        limits.max_frame_bytes = limits.max_output_bytes_per_connection + 1;
      },
      [](AutomationLimits& limits) {
        limits.max_filename_utf16_units = limits.max_path_utf16_units + 1;
      },
      [](AutomationLimits& limits) {
        limits.max_request_key_bytes =
            static_cast<std::uint32_t>(limits.max_frame_bytes + 1);
      },
      [](AutomationLimits& limits) {
        limits.max_opaque_handle_bytes =
            static_cast<std::uint32_t>(limits.max_frame_bytes + 1);
      },
      [](AutomationLimits& limits) {
        limits.default_request_timeout = limits.max_request_timeout + 1ms;
      },
      [](AutomationLimits& limits) {
        limits.default_longshot_timeout = limits.max_request_timeout + 1ms;
      },
      [](AutomationLimits& limits) {
        limits.max_request_timeout = 24h + 1ms;
      }};
  for (std::size_t index = 0; index < std::size(mutations); ++index) {
    SCOPED_TRACE(index);
    AutomationLimits limits;
    mutations[index](limits);
    EXPECT_FALSE(limits.valid());
  }
}

TEST(AutomationContractTest, FrameBudgetFitsTheProtocolLengthField) {
  AutomationLimits limits;
  limits.max_frame_bytes = (std::numeric_limits<std::uint32_t>::max)();
  limits.max_output_bytes_per_connection = limits.max_frame_bytes + 1;
  EXPECT_TRUE(limits.valid());
  ++limits.max_frame_bytes;
  EXPECT_FALSE(limits.valid());

  limits = AutomationLimits{};
  limits.max_request_timeout = 24h;
  EXPECT_TRUE(limits.valid());
}

TEST(AutomationContractTest, CapturePixelBudgetValidationCannotOverflow) {
  AutomationLimits limits;
  limits.max_capture_pixels = (std::numeric_limits<std::uint64_t>::max)();
  limits.max_result_bytes = (std::numeric_limits<std::uint64_t>::max)();
  limits.max_retained_result_bytes = (std::numeric_limits<std::uint64_t>::max)();
  EXPECT_FALSE(limits.valid());
  limits.max_capture_pixels = limits.max_result_bytes / 4;
  EXPECT_TRUE(limits.valid());
}

TEST(AutomationContractTest, AllErrorCodesRetainStableNumbersAndSymbols) {
  struct Mapping {
    int code;
    int expected_number;
    std::string_view expected_symbol;
  };
  const Mapping mappings[]{
      {ErrorCode::kOk, 0, "Ok"},
      {ErrorCode::kUnknown, 1, "Unknown"},
      {ErrorCode::kNotReady, 10, "NotReady"},
      {ErrorCode::kInvalidArgument, 20, "InvalidArgument"},
      {ErrorCode::kCaptureFailed, 30, "CaptureFailed"},
      {ErrorCode::kWindowNotFound, 40, "WindowNotFound"},
      {ErrorCode::kWindowAmbiguous, 41, "WindowAmbiguous"},
      {ErrorCode::kLongShotUnsupported, 50, "LongShotUnsupported"},
      {ErrorCode::kExportFailed, 60, "ExportFailed"},
      {ErrorCode::kCommandUnmatched, 70, "CommandUnmatched"},
      {ErrorCode::kCancelled, 80, "Cancelled"},
      {ErrorCode::kTimeout, 81, "Timeout"},
      {ErrorCode::kBusy, 82, "Busy"},
      {ErrorCode::kResultExpired, 83, "ResultExpired"},
      {ErrorCode::kResultNotFound, 84, "ResultNotFound"},
      {ErrorCode::kAccessDenied, 85, "AccessDenied"},
      {ErrorCode::kResourceLimit, 86, "ResourceLimit"},
      {ErrorCode::kShuttingDown, 87, "ShuttingDown"},
      {ErrorCode::kOperationNotFound, 88, "OperationNotFound"},
      {ErrorCode::kConflict, 89, "Conflict"},
      {ErrorCode::kNotImplemented, 100, "NotImplemented"}};
  for (const auto& mapping : mappings) {
    SCOPED_TRACE(mapping.expected_symbol);
    EXPECT_EQ(mapping.code, mapping.expected_number);
    EXPECT_EQ(errorCodeSymbol(mapping.code), mapping.expected_symbol);
  }
  EXPECT_EQ(errorCodeSymbol(-1), "Unknown");
  EXPECT_EQ(errorCodeSymbol(42), "Unknown");
  EXPECT_EQ(errorCodeSymbol((std::numeric_limits<int>::max)()), "Unknown");
}

TEST(AutomationContractTest, OnlyCompletedOperationStatesAreTerminal) {
  const std::array<OperationState, 6> active{
      OperationState::Queued, OperationState::AwaitingUser, OperationState::Running,
      OperationState::Paused, OperationState::Finalizing, OperationState::Cancelling};
  for (const auto state : active) {
    SCOPED_TRACE(static_cast<int>(state));
    EXPECT_FALSE(isTerminal(state));
  }
  const std::array<OperationState, 4> terminal{
      OperationState::Succeeded, OperationState::Failed, OperationState::Cancelled,
      OperationState::TimedOut};
  for (const auto state : terminal) {
    SCOPED_TRACE(static_cast<int>(state));
    EXPECT_TRUE(isTerminal(state));
  }
  EXPECT_FALSE(isTerminal(static_cast<OperationState>(99)));
}

TEST(AutomationContractTest, FailedOperationCanBeReturnedBySuccessfulQuery) {
  OperationSnapshot operation;
  operation.operation_id = 9;
  operation.state = OperationState::Failed;
  operation.outcome = ActionResult{};
  operation.outcome->operation_id = 9;
  operation.outcome->error_code = ErrorCode::kCaptureFailed;
  operation.outcome->failure_stage = "capture";

  AutomationResponse response;
  response.connection = AutomationConnection{31, 5};
  response.result.request_id = 17;
  response.result.operation_id = 9;
  response.result.ok = true;
  response.result.error_code = ErrorCode::kOk;
  response.control = operation;

  EXPECT_TRUE(response.result.ok);
  EXPECT_EQ(response.result.request_id, 17u);
  const auto& snapshot = std::get<OperationSnapshot>(response.control);
  EXPECT_EQ(snapshot.state, OperationState::Failed);
  ASSERT_TRUE(snapshot.outcome.has_value());
  EXPECT_FALSE(snapshot.outcome->ok);
  EXPECT_EQ(snapshot.outcome->error_code, ErrorCode::kCaptureFailed);
  EXPECT_EQ(snapshot.outcome->failure_stage, "capture");
}

TEST(AutomationContractTest, ResultExpiryIsIndependentOfSuccessfulOperationOutcome) {
  OperationSnapshot operation;
  operation.operation_id = 9;
  operation.state = OperationState::Succeeded;
  operation.committed = true;
  operation.result_availability = ResultAvailability::Available;
  operation.outcome = ActionResult{};
  operation.outcome->ok = true;
  operation.outcome->error_code = ErrorCode::kOk;
  CapturedResult captured;
  captured.result_id = 23;
  captured.width = 640;
  captured.height = 480;
  captured.bounds = ScreenPhysicalRect{-50, 10, 640, 480};
  captured.expires_at = std::chrono::steady_clock::now();
  operation.outcome->output = captured;

  for (const auto availability :
       {ResultAvailability::Released, ResultAvailability::Expired}) {
    OperationSnapshot retained = operation;
    retained.result_availability = availability;
    EXPECT_EQ(retained.state, OperationState::Succeeded);
    EXPECT_TRUE(retained.committed);
    ASSERT_TRUE(retained.outcome.has_value());
    EXPECT_TRUE(retained.outcome->ok);
    EXPECT_EQ(retained.outcome->error_code, ErrorCode::kOk);
    const auto& metadata = std::get<CapturedResult>(retained.outcome->output);
    EXPECT_EQ(metadata.result_id, 23u);
    EXPECT_EQ(metadata.bounds, captured.bounds);
    EXPECT_EQ(metadata.expires_at, captured.expires_at);
  }
}

TEST(AutomationContractTest, UnreachableStatusKeepsApplicationFactsUnknown) {
  const StatusInfo status;
  EXPECT_FALSE(status.reachable);
  EXPECT_FALSE(status.app_running.has_value());
  EXPECT_FALSE(status.automation_enabled.has_value());
  EXPECT_FALSE(status.busy.has_value());
  EXPECT_FALSE(status.limits.has_value());
  EXPECT_FALSE(status.resources.has_value());
}

}  // namespace
