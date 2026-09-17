#include "qingying/app/capture_service.h"
#include "qingying/app/result_store.h"
#include "qingying/app/export_executor.h"
#include <mutex>
#include <vector>
#include "qingying/app/action_handlers.hpp"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/export_service.hpp"
#include "qingying/app/result_action_service.h"
#include "qingying/pin/pin_manager.hpp"
#include <gtest/gtest.h>
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <thread>

namespace qingying {
namespace {
using namespace std::chrono_literals;

bool pumpUntil(const std::function<bool()>& ready, std::chrono::milliseconds budget = 2s) {
  const auto deadline = std::chrono::steady_clock::now() + budget;
  do {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) continue;
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (ready()) return true;
    Sleep(1);
  } while (std::chrono::steady_clock::now() < deadline);
  return ready();
}
struct ReleaseEvent {
  HANDLE event;
  ~ReleaseEvent() { SetEvent(event); }
};
struct Event {
  HANDLE handle{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  ~Event() { CloseHandle(handle); }
};
ActionResult success(Image& image) {
  image = Image{1, 1, {0xFF123456}};
  ActionResult result;
  result.ok = true;
  result.error_code = ErrorCode::kOk;
  return result;
}

TEST(AsyncCaptureServiceTest, CaptureRunsOffUiAndPublishesOnUiThroughDispatcher) {
  CaptureEngine engine;
  ResultStore results;
  PinManager pins;
  InteractionGate gate;
  const auto ui = std::this_thread::get_id();
  std::thread::id provider_thread;
  CaptureService service(engine, results, pins, gate,
      [&](const ScreenPhysicalRect&, Image& image) {
        provider_thread = std::this_thread::get_id();
        return success(image);
      });
  ExportService exports;
  ResultActionService actions(results, exports, pins);
  ActionDispatcher dispatcher;
  registerAppHandlers(dispatcher, service, results, actions);
  auto request = makeActionRequest(CaptureRegionRequest{{0, 0, 1, 1}});
  request.context.result_scope = 2;
  request.request_id = 55;
  request.operation_id = 56;
  int callbacks = 0;
  ActionResult result;
  dispatcher.submit(request, [&](ActionResult value) {
    EXPECT_EQ(std::this_thread::get_id(), ui);
    EXPECT_FALSE(pins.captureExclusionActive());
    result = std::move(value);
    ++callbacks;
  });
  ASSERT_TRUE(pumpUntil([&] { return callbacks != 0; }));
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.request_id, 55u);
  EXPECT_NE(provider_thread, ui);
  EXPECT_EQ(callbacks, 1);
  EXPECT_NE(results.currentId(2), kInvalidResultId);
  EXPECT_EQ(results.budgetSnapshot().reserved_bytes, 0u);
  EXPECT_FALSE(gate.busy());
}

TEST(AsyncCaptureServiceTest, SlowProvidersLeaveUiMessagesResponsive) {
  for (const auto delay : {100ms, 500ms}) {
    CaptureEngine engine;
    ResultStore results;
    PinManager pins;
    InteractionGate gate;
    Event release;
    std::atomic_bool entered{false};
    CaptureService service(engine, results, pins, gate,
        [&](const ScreenPhysicalRect&, Image& image) {
          entered.store(true);
          WaitForSingleObject(release.handle, static_cast<DWORD>(delay.count()));
          return success(image);
        });
    ReleaseEvent unblock{release.handle};
    int callbacks = 0;
    auto request = makeActionRequest(CaptureRegionRequest{{0, 0, 1, 1}});
    service.captureAsync(request, {0, 0, 1, 1}, [&](ActionResult result) {
      EXPECT_TRUE(result.ok);
      ++callbacks;
    });
    ASSERT_TRUE(pumpUntil([&] { return entered.load(); }));
    EXPECT_EQ(callbacks, 0);
    EXPECT_TRUE(pins.captureExclusionActive());
    EXPECT_TRUE(gate.busy());
    // A synchronous UI call remains usable while the provider is suspended.
    HWND window = CreateWindowExW(0, L"STATIC", L"responsive", WS_POPUP,
                                  0, 0, 20, 20, nullptr, nullptr, nullptr, nullptr);
    ASSERT_NE(window, nullptr);
    EXPECT_TRUE(SetWindowPos(window, nullptr, 40, 40, 20, 20, SWP_NOZORDER));
    EXPECT_TRUE(DestroyWindow(window));
    EXPECT_EQ(callbacks, 0);
    ASSERT_TRUE(pumpUntil([&] { return callbacks == 1; }));
  }
}

TEST(AsyncCaptureServiceTest, CancellationReleasesUiGuardsBeforeBlockedProviderReturns) {
  CaptureEngine engine;
  ResultStore results;
  PinManager pins;
  InteractionGate gate;
  Event release;
  std::atomic_bool entered{false};
  CaptureService service(engine, results, pins, gate,
      [&](const ScreenPhysicalRect&, Image& image) {
        entered.store(true);
        WaitForSingleObject(release.handle, INFINITE);
        return success(image);
      });
  ReleaseEvent unblock{release.handle};
  CancellationSource cancellation;
  auto request = makeActionRequest(CaptureRegionRequest{{0, 0, 1, 1}});
  request.cancellation = cancellation.token();
  int callbacks = 0;
  service.captureAsync(request, {0, 0, 1, 1}, [&](ActionResult result) {
    EXPECT_EQ(result.error_code, ErrorCode::kCancelled);
    ++callbacks;
  });
  ASSERT_TRUE(pumpUntil([&] { return entered.load(); }));
  cancellation.cancel();
  ASSERT_TRUE(pumpUntil([&] { return callbacks == 1; }));
  EXPECT_FALSE(pins.captureExclusionActive());
  EXPECT_FALSE(gate.busy());
  EXPECT_EQ(results.budgetSnapshot().reserved_bytes, 0u);
  EXPECT_EQ(results.currentId(), kInvalidResultId);
  service.beginStop();
  EXPECT_FALSE(service.joinUntil(std::chrono::steady_clock::now() + 10ms));
  EXPECT_NE(service.diagnosticSnapshot().find("thread=capture_worker"), std::string::npos);
  SetEvent(release.handle);
  EXPECT_TRUE(service.joinUntil(std::chrono::steady_clock::now() + 2s));
  pumpUntil([] { return true; });
  EXPECT_EQ(callbacks, 1);
  EXPECT_EQ(results.currentId(), kInvalidResultId);
}

TEST(AsyncCaptureServiceTest, DeadlineAndRepeatedShutdownDoNotPublishLateCapture) {
  CaptureEngine engine;
  ResultStore results;
  PinManager pins;
  InteractionGate gate;
  Event release;
  std::atomic_bool entered{false};
  CaptureService service(engine, results, pins, gate,
      [&](const ScreenPhysicalRect&, Image& image) {
        entered.store(true);
        WaitForSingleObject(release.handle, INFINITE);
        return success(image);
      });
  ReleaseEvent unblock{release.handle};
  auto request = makeActionRequest(CaptureRegionRequest{{0, 0, 1, 1}});
  request.timeout = 100ms;
  int callbacks = 0;
  service.captureAsync(request, {0, 0, 1, 1}, [&](ActionResult result) {
    EXPECT_EQ(result.error_code, ErrorCode::kTimeout);
    ++callbacks;
  });
  ASSERT_TRUE(pumpUntil([&] { return entered.load(); }));
  ASSERT_TRUE(pumpUntil([&] { return callbacks == 1; }));
  service.beginStop();
  service.beginStop();
  EXPECT_FALSE(service.joinUntil(std::chrono::steady_clock::now() + 10ms));
  SetEvent(release.handle);
  ASSERT_TRUE(service.joinUntil(std::chrono::steady_clock::now() + 2s));
  pumpUntil([] { return true; });
  EXPECT_EQ(callbacks, 1);
  EXPECT_EQ(results.currentId(), kInvalidResultId);
}

TEST(AsyncCaptureServiceTest, BusyBudgetAndProviderExceptionPreserveResourceInvariants) {
  AutomationLimits limits;
  limits.max_capture_pixels = 4;
  CaptureEngine engine;
  ResultStore results(limits);
  PinManager pins;
  InteractionGate gate;
  CaptureService service(engine, results, pins, gate,
      [](const ScreenPhysicalRect&, Image&) -> ActionResult {
        throw std::runtime_error("injected provider failure");
      });
  const auto old = results.publish(2, Image{1, 1, {7}});
  int calls = 0;
  auto guard = gate.acquire(InteractionKind::SaveDialog);
  auto request = makeActionRequest(CaptureRegionRequest{{0, 0, 1, 1}});
  request.context.result_scope = 2;
  service.captureAsync(request,
      {0, 0, 1, 1}, [&](ActionResult result) {
    EXPECT_EQ(result.error_code, ErrorCode::kBusy);
    ++calls;
  });
  EXPECT_EQ(results.currentId(2), old);
  guard.reset();
  service.captureAsync(request,
      {0, 0, 5, 1}, [&](ActionResult result) {
    EXPECT_EQ(result.error_code, ErrorCode::kResourceLimit);
    ++calls;
  });
  EXPECT_EQ(results.currentId(2), old);
  service.captureAsync(request,
      {0, 0, 1, 1}, [&](ActionResult result) {
    EXPECT_EQ(result.error_code, ErrorCode::kCaptureFailed);
    ++calls;
  });
  ASSERT_TRUE(pumpUntil([&] { return calls == 3; }));
  EXPECT_FALSE(pins.captureExclusionActive());
  EXPECT_FALSE(gate.busy());
  EXPECT_EQ(results.budgetSnapshot().reserved_bytes, 0u);
  EXPECT_EQ(results.currentId(2), kInvalidResultId);
}

TEST(AsyncCaptureServiceTest, RawCaptureTransformRunsOnWorkerAndPreservesExistingResult) {
  CaptureEngine engine;
  ResultStore results;
  PinManager pins;
  InteractionGate gate;
  CaptureService service(engine, results, pins, gate,
      [](const ScreenPhysicalRect&, Image& image) { return success(image); });
  const auto retained = results.publish(Image{1, 1, {7}});
  const auto ui = std::this_thread::get_id();
  bool completed = false;
  service.captureImageAsync(makeActionRequest(CaptureRegionRequest{{0, 0, 1, 1}}),
      {0, 0, 1, 1}, [&](ActionResult result, Image image) {
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(image.pixels[0], 8u);
    EXPECT_EQ(std::this_thread::get_id(), ui);
    completed = true;
  }, nullptr, [ui](Image& image) {
    EXPECT_NE(std::this_thread::get_id(), ui);
    image.pixels[0] = 8;
  });
  ASSERT_TRUE(pumpUntil([&] { return completed; }));
  EXPECT_EQ(results.currentId(), retained);
  EXPECT_EQ(results.budgetSnapshot().reserved_bytes, 0u);
}
TEST(AsyncCaptureServiceTest, WindowDiscoveryRunsOffUiAndRejectsMovedWindow) {
  struct Window {
    HWND handle{CreateWindowExW(0, L"STATIC", L"capture target", WS_POPUP,
                               30, 40, 10, 10, nullptr, nullptr, nullptr, nullptr)};
    ~Window() { DestroyWindow(handle); }
  } target;
  ASSERT_NE(target.handle, nullptr);
  CaptureEngine engine;
  ResultStore results;
  PinManager pins;
  InteractionGate gate;
  Event release;
  std::atomic_bool entered{false};
  const auto ui = std::this_thread::get_id();
  CaptureService service(engine, results, pins, gate,
      [&](const ScreenPhysicalRect& region, Image& image) {
        entered.store(true);
        WaitForSingleObject(release.handle, INFINITE);
        image = Image{region.width, region.height,
                      std::vector<std::uint32_t>(region.width * region.height, 7)};
        ActionResult result;
        result.ok = true;
        return result;
      }, {}, [&] {
        EXPECT_NE(std::this_thread::get_id(), ui);
        RECT rect{};
        GetWindowRect(target.handle, &rect);
        return WindowCatalogSnapshot{{WindowCatalogEntry{
            reinterpret_cast<std::uintptr_t>(target.handle), GetCurrentProcessId(),
            L"capture target", {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top}}}};
      });
  ReleaseEvent unblock{release.handle};
  auto request = makeActionRequest(CaptureWindowRequest{L"capture target"});
  int callbacks = 0;
  service.captureActionAsync(request, [&](ActionResult result) {
    EXPECT_EQ(result.error_code, ErrorCode::kWindowNotFound) << result.message;
    ++callbacks;
  });
  ASSERT_TRUE(pumpUntil([&] { return entered.load(); }));
  EXPECT_EQ(callbacks, 0);
  EXPECT_TRUE(SetWindowPos(target.handle, nullptr, 60, 70, 10, 10, SWP_NOZORDER));
  SetEvent(release.handle);
  ASSERT_TRUE(pumpUntil([&] { return callbacks == 1; }));
  EXPECT_EQ(results.currentId(), kInvalidResultId);
  EXPECT_FALSE(gate.busy());
  EXPECT_FALSE(pins.captureExclusionActive());
}

TEST(AsyncCaptureServiceTest, GuiPngSaveKeepsLeaseAndEncodesOffUi) {
  ExportService exports;
  ResultStore results;
  PinManager pins;
  InteractionGate gate;
  const auto id = results.publish(Image{1, 1, {7}});
  const auto ui = std::this_thread::get_id();
  Event release;
  std::atomic_bool entered{false};
  std::mutex mutex;
  std::vector<ActionTask> notifications;
  ResultActionService actions(results, exports, pins,
      [](HWND) { return std::optional<std::wstring>{L"injected.png"}; }, &gate, nullptr,
      [&](const Image& image, ResultId actual, const std::wstring&, bool,
          ResultActionService::CommitAuthorization authorize) {
        EXPECT_NE(std::this_thread::get_id(), ui);
        entered.store(true);
        WaitForSingleObject(release.handle, INFINITE);
        EXPECT_EQ(image.pixels[0], 7u);
        EXPECT_EQ(actual, id);
        ActionResult result;
        result.ok = authorize();
        return result;
      });
  ExportExecutor executor(1);
  ReleaseEvent unblock{release.handle};
  actions.setExportExecutor(executor, [&](ActionTask task) {
    std::lock_guard<std::mutex> lock(mutex);
    notifications.push_back(std::move(task));
  });
  const auto queued = actions.save(id);
  EXPECT_TRUE(queued.ok);
  EXPECT_EQ(queued.message, "save queued");
  ASSERT_TRUE(pumpUntil([&] { return entered.load(); }));
  results.clear();
  EXPECT_FALSE(gate.busy());
  SetEvent(release.handle);
  ASSERT_TRUE(pumpUntil([&] {
    std::lock_guard<std::mutex> lock(mutex);
    return !notifications.empty();
  }));
  // Worker result notification is explicitly delivered on UI.
  for (auto& task : notifications) task();
  executor.shutdown();
}

}  // namespace
}  // namespace qingying
