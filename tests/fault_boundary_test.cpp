#include "qingying/diagnostics/fault_boundary.h"
#include "qingying/app/export_executor.h"
#include "qingying/automation/operation_registry.h"
#include "automation_wire_codec.h"
#include "qingying/app/tray_controller.hpp"
#include <Windows.h>
#include <gtest/gtest.h>
#include <future>
#include <stdexcept>

namespace qingying {
TEST(FaultBoundaryTest, CancellationNormalizesFaultCodeAndSettlesOnce) {
  OperationRegistry registry{317};
  const auto context = registry.connect(7);
  ASSERT_TRUE(context);
  AutomationRequest request;
  request.request_id = 17;
  request.payload = ExecuteActionRequest{StatusRequest{}, std::nullopt};
  const auto submission = registry.begin(*context, request);
  ASSERT_TRUE(submission.ok());
  ASSERT_TRUE(submission.control->requestCancel(AbortReason::ClientCancel));
  ActionResult outcome;
  outcome.error_code = ErrorCode::kUnknown;
  outcome.diagnostic = recordFault(outcome.error_code, FaultOrigin::Handler,
      FaultDomain::Request, "test.provider",
      {317, context->connection.generation, 7, 17, submission.operation_id});
  ActionResult settled;
  ASSERT_TRUE(registry.complete(*context, submission.operation_id, outcome, &settled));
  EXPECT_FALSE(registry.complete(*context, submission.operation_id, outcome));
  EXPECT_EQ(settled.error_code, ErrorCode::kCancelled);
  ASSERT_TRUE(settled.diagnostic);
  EXPECT_EQ(settled.diagnostic->error_code, settled.error_code);
  ipc::WireResponse wire;
  wire.rpc_id = "17";
  wire.response.connection = context->connection;
  wire.response.result = settled;
  EXPECT_TRUE(ipc::encodeFrame(wire));
}
TEST(FaultBoundaryTest, ResourceFailureAndNestedScopesHaveStableMetadata) {
  DiagnosticScope outer({17, 3, 23, 41, 42, std::chrono::steady_clock::now()});
  {
    FaultContext inner; inner.operation_id = 99;
    DiagnosticScope nested(inner);
    FaultDiagnostic diagnostic;
    EXPECT_FALSE(containFault(FaultOrigin::Handler, FaultDomain::Request,
        [] { throw std::bad_alloc{}; }, &diagnostic, "test.provider"));
    EXPECT_EQ(diagnostic.error_code, ErrorCode::kResourceLimit);
    EXPECT_STREQ(diagnostic.correlation_id.data(), "a17-s3-r41");
    EXPECT_EQ(diagnostic.context.operation_id, 99u);
    EXPECT_FALSE(diagnostic.retryable);
  }
  EXPECT_EQ(currentFaultContext().operation_id, 42u);
}
TEST(FaultBoundaryTest, RecordsAreBoundedAndDoNotContainExceptionText) {
  for (std::uint64_t i = 0; i < 300; ++i) {
    DiagnosticScope scope({18, 4, 24, i + 1});
    containFault(FaultOrigin::Ui, FaultDomain::Request,
        [] { throw std::runtime_error("private window title C:\\secret\\capture.png"); });
  }
  const auto records = recentFaults();
  ASSERT_EQ(records.size(), 256u);
  EXPECT_EQ(records.front().context.request_id, 45u);
  EXPECT_EQ(records.back().context.request_id, 300u);
  EXPECT_STREQ(records.back().provider_id.data(), "");
}
TEST(FaultBoundaryTest, WorkerKeepsCorrelationAndContinuesAfterExecuteAndRejectThrow) {
  ExportExecutor executor(8);
  std::promise<void> rejected, finished;
  DiagnosticScope scope({19, 5, 25, 71, 72, std::chrono::steady_clock::now()});
  ASSERT_TRUE(executor.submit([] { throw std::runtime_error("private payload"); }, [&] {
    rejected.set_value(); throw std::runtime_error("private reject");
  }, 71));
  ASSERT_TRUE(executor.submit([&] { finished.set_value(); }));
  EXPECT_EQ(rejected.get_future().wait_for(std::chrono::seconds{2}), std::future_status::ready);
  EXPECT_EQ(finished.get_future().wait_for(std::chrono::seconds{2}), std::future_status::ready);
  executor.shutdown();
  const auto records = recentFaults();
  ASSERT_GE(records.size(), 2u);
  EXPECT_STREQ(records.back().correlation_id.data(), "a19-s5-r71");
  EXPECT_EQ(records.back().origin, FaultOrigin::Worker);
}
TEST(FaultBoundaryTest, TrayFilterExceptionDoesNotEscapeWndProcOrBlockNextMessage) {
  TrayController tray;
  ASSERT_TRUE(tray.create(GetModuleHandleW(nullptr)));
  int handled = 0;
  tray.setMessageFilter([&](UINT message, WPARAM, LPARAM, LRESULT*) {
    if (message == WM_APP + 77 || message == WM_PAINT)
      throw std::runtime_error("private UI task");
    if (message == WM_APP + 78) { ++handled; return true; }
    return false;
  });
  EXPECT_EQ(SendMessageW(tray.hwnd(), WM_APP + 77, 0, 0), 0);
  InvalidateRect(tray.hwnd(), nullptr, FALSE);
  EXPECT_EQ(SendMessageW(tray.hwnd(), WM_PAINT, 0, 0), 0);
  EXPECT_FALSE(GetUpdateRect(tray.hwnd(), nullptr, FALSE));
  SendMessageW(tray.hwnd(), WM_APP + 78, 0, 0);
  EXPECT_EQ(handled, 1);
  tray.destroy();
  MSG message{};
  while (PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
}
}  // namespace qingying
