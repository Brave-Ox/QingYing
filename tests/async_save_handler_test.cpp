#include "qingying/app/action_handlers.hpp"

#include "qingying/app/export_executor.h"
#include "qingying/app/result_action_service.h"
#include "qingying/app/save_policy.h"
#include "qingying/automation/automation_contract.h"
#include "qingying/export/export_service.hpp"
#include "qingying/pin/pin_manager.hpp"

#include <Windows.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace qingying {
namespace {

class TemporaryDirectory final {
 public:
  TemporaryDirectory() {
    wchar_t base[MAX_PATH] = {};
    wchar_t seed[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, base) == 0 ||
        GetTempFileNameW(base, L"qas", 0, seed) == 0) return;
    DeleteFileW(seed);
    if (CreateDirectoryW(seed, nullptr)) path_ = seed;
  }
  ~TemporaryDirectory() {
    if (!path_.empty()) std::filesystem::remove_all(path_);
  }
  std::wstring file(const wchar_t* name) const {
    return (std::filesystem::path(path_) / name).wstring();
  }
  const std::wstring& path() const noexcept { return path_; }
 private:
  std::wstring path_;
};

class FakeControl final : public IOperationControl {
 public:
  bool requestCancel(AbortReason reason) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.committed || status_.abort_reason != AbortReason::None) return false;
    status_.abort_reason = reason;
    return true;
  }
  bool tryCommit() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.committed || status_.abort_reason != AbortReason::None) return false;
    status_.committed = true;
    return true;
  }
  OperationControlStatus status() override {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
  }
 private:
  std::mutex mutex_;
  OperationControlStatus status_;
};

ActionResult saved(ResultId id, const std::wstring& path) {
  ActionResult result;
  result.ok = true;
  result.error_code = ErrorCode::kOk;
  result.output = SavedResult{id, path, ImageFormat::Png};
  return result;
}

struct BlockingTransaction {
  std::mutex mutex;
  std::condition_variable wake;
  int entered{0};
  bool release{false};
  bool hold_after_commit{false};
  bool committed{false};

  ActionResult run(const Image&, ResultId id, const std::wstring& path, bool,
                   ResultActionService::CommitAuthorization authorize) {
    {
      std::unique_lock<std::mutex> lock(mutex);
      ++entered;
      wake.notify_all();
      wake.wait(lock, [&] { return release; });
    }
    if (!authorize || !authorize()) {
      ActionResult result;
      result.error_code = ErrorCode::kCancelled;
      return result;
    }
    {
      std::unique_lock<std::mutex> lock(mutex);
      committed = true;
      wake.notify_all();
      if (hold_after_commit) wake.wait(lock, [&] { return !hold_after_commit; });
    }
    return saved(id, path);
  }
};

ActionRequest saveRequest(ResultScopeId scope, ResultId id,
                          std::wstring path,
                          std::shared_ptr<IOperationControl> control = {}) {
  ActionRequest request{SaveRequest{ResultSelection::specific(id),
                                    std::move(path)}};
  request.context.result_scope = scope;
  request.operation_control = std::move(control);
  return request;
}

TEST(AsyncSaveHandlerTest, LeaseSurvivesScopeClearAndCancellationCompletesOnce) {
  TemporaryDirectory directory;
  ASSERT_FALSE(directory.path().empty());
  ResultStore store;
  ExportService exporter;
  PinManager pins;
  SavePolicy policy({directory.path()});
  BlockingTransaction transaction;
  ResultActionService actions(store, exporter, pins, {}, nullptr, &policy,
      [&](const Image& image, ResultId id, const std::wstring& path, bool overwrite,
          ResultActionService::CommitAuthorization authorize) {
        return transaction.run(image, id, path, overwrite, std::move(authorize));
      });
  ExportExecutor executor(2);
  ActionDispatcher dispatcher;
  registerAsyncSaveHandler(dispatcher, actions, executor);
  constexpr ResultScopeId scope = 2;
  const auto id = store.publish(scope, Image{2, 1, {1, 2}});
  const auto budget = store.budgetObserver();
  auto control = std::make_shared<FakeControl>();
  std::mutex result_mutex;
  std::condition_variable result_wake;
  int completions = 0;
  ActionResult outcome;
  dispatcher.submit(saveRequest(scope, id, directory.file(L"cancel.png"), control),
      [&](ActionResult result) {
        std::lock_guard<std::mutex> lock(result_mutex);
        outcome = std::move(result);
        ++completions;
        result_wake.notify_all();
      });
  {
    std::unique_lock<std::mutex> lock(transaction.mutex);
    ASSERT_TRUE(transaction.wake.wait_for(lock, 2s,
        [&] { return transaction.entered == 1; }));
  }
  store.clearScope(scope);
  EXPECT_GT(budget.snapshot().retained_bytes, 0u);
  EXPECT_TRUE(control->requestCancel(AbortReason::ClientCancel));
  {
    std::lock_guard<std::mutex> lock(transaction.mutex);
    transaction.release = true;
  }
  transaction.wake.notify_all();
  {
    std::unique_lock<std::mutex> lock(result_mutex);
    ASSERT_TRUE(result_wake.wait_for(lock, 2s, [&] { return completions == 1; }));
  }
  executor.shutdown();
  EXPECT_EQ(outcome.error_code, ErrorCode::kCancelled);
  EXPECT_EQ(completions, 1);
  EXPECT_EQ(budget.snapshot().retained_bytes, 0u);
}

TEST(AsyncSaveHandlerTest, QueueLimitAndQueuedTimeoutRemainDeterministic) {
  TemporaryDirectory directory;
  ResultStore store;
  ExportService exporter;
  PinManager pins;
  SavePolicy policy({directory.path()});
  BlockingTransaction transaction;
  ResultActionService actions(store, exporter, pins, {}, nullptr, &policy,
      [&](const Image& image, ResultId id, const std::wstring& path, bool overwrite,
          ResultActionService::CommitAuthorization authorize) {
        return transaction.run(image, id, path, overwrite, std::move(authorize));
      });
  ExportExecutor executor(1);
  ActionDispatcher dispatcher;
  registerAsyncSaveHandler(dispatcher, actions, executor);
  constexpr ResultScopeId scope = 3;
  const auto id = store.publish(scope, Image{1, 1, {1}});
  std::mutex mutex;
  std::condition_variable wake;
  std::vector<ActionResult> results;
  auto complete = [&](ActionResult result) {
    std::lock_guard<std::mutex> lock(mutex);
    results.push_back(std::move(result));
    wake.notify_all();
  };
  dispatcher.submit(saveRequest(scope, id, directory.file(L"one.png")), complete);
  {
    std::unique_lock<std::mutex> lock(transaction.mutex);
    ASSERT_TRUE(transaction.wake.wait_for(lock, 2s,
        [&] { return transaction.entered == 1; }));
  }
  auto queued = saveRequest(scope, id, directory.file(L"two.png"));
  queued.timeout = 30ms;
  dispatcher.submit(queued, complete);
  dispatcher.submit(saveRequest(scope, id, directory.file(L"three.png")), complete);
  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(wake.wait_for(lock, 2s, [&] { return results.size() == 1; }));
    EXPECT_EQ(results.front().error_code, ErrorCode::kResourceLimit);
  }
  std::this_thread::sleep_for(40ms);
  {
    std::lock_guard<std::mutex> lock(transaction.mutex);
    transaction.release = true;
  }
  transaction.wake.notify_all();
  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(wake.wait_for(lock, 2s, [&] { return results.size() == 3; }));
  }
  executor.shutdown();
  EXPECT_EQ(std::count_if(results.begin(), results.end(), [](const auto& result) {
    return result.error_code == ErrorCode::kTimeout;
  }), 1);
}

TEST(AsyncSaveHandlerTest, CommitWinsLateCancelAndShutdownJoinsWorker) {
  TemporaryDirectory directory;
  ResultStore store;
  ExportService exporter;
  PinManager pins;
  SavePolicy policy({directory.path()});
  BlockingTransaction transaction;
  transaction.release = true;
  transaction.hold_after_commit = true;
  ResultActionService actions(store, exporter, pins, {}, nullptr, &policy,
      [&](const Image& image, ResultId id, const std::wstring& path, bool overwrite,
          ResultActionService::CommitAuthorization authorize) {
        return transaction.run(image, id, path, overwrite, std::move(authorize));
      });
  ExportExecutor executor(1);
  ActionDispatcher dispatcher;
  registerAsyncSaveHandler(dispatcher, actions, executor);
  constexpr ResultScopeId scope = 4;
  const auto id = store.publish(scope, Image{1, 1, {7}});
  auto control = std::make_shared<FakeControl>();
  std::atomic_int completions{0};
  ActionResult outcome;
  dispatcher.submit(saveRequest(scope, id, directory.file(L"committed.png"), control),
      [&](ActionResult result) {
        outcome = std::move(result);
        ++completions;
      });
  {
    std::unique_lock<std::mutex> lock(transaction.mutex);
    ASSERT_TRUE(transaction.wake.wait_for(lock, 2s,
        [&] { return transaction.committed; }));
  }
  EXPECT_FALSE(control->requestCancel(AbortReason::ClientCancel));
  std::thread shutdown([&] { executor.shutdown(); });
  {
    std::lock_guard<std::mutex> lock(transaction.mutex);
    transaction.hold_after_commit = false;
  }
  transaction.wake.notify_all();
  shutdown.join();
  EXPECT_EQ(completions.load(), 1);
  EXPECT_TRUE(outcome.ok);
  EXPECT_EQ(std::get<SavedResult>(outcome.output).result_id, id);
}

TEST(AsyncSaveHandlerTest, ShutdownSettlesRunningAndQueuedSavesExactlyOnce) {
  TemporaryDirectory directory;
  ResultStore store;
  ExportService exporter;
  PinManager pins;
  SavePolicy policy({directory.path()});
  BlockingTransaction transaction;
  ResultActionService actions(store, exporter, pins, {}, nullptr, &policy,
      [&](const Image& image, ResultId id, const std::wstring& path, bool overwrite,
          ResultActionService::CommitAuthorization authorize) {
        return transaction.run(image, id, path, overwrite, std::move(authorize));
      });
  ExportExecutor executor(1);
  ActionDispatcher dispatcher;
  registerAsyncSaveHandler(dispatcher, actions, executor);
  constexpr ResultScopeId scope = 5;
  const auto id = store.publish(scope, Image{1, 1, {9}});
  std::mutex mutex;
  std::condition_variable wake;
  std::vector<ActionResult> results;
  auto complete = [&](ActionResult result) {
    std::lock_guard<std::mutex> lock(mutex);
    results.push_back(std::move(result));
    wake.notify_all();
  };
  dispatcher.submit(saveRequest(scope, id, directory.file(L"running.png")), complete);
  {
    std::unique_lock<std::mutex> lock(transaction.mutex);
    ASSERT_TRUE(transaction.wake.wait_for(lock, 2s,
        [&] { return transaction.entered == 1; }));
  }
  dispatcher.submit(saveRequest(scope, id, directory.file(L"queued.png")), complete);
  std::thread shutdown([&] { executor.shutdown(); });
  while (!executor.stopping()) std::this_thread::yield();
  {
    std::lock_guard<std::mutex> lock(transaction.mutex);
    transaction.release = true;
  }
  transaction.wake.notify_all();
  shutdown.join();
  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(wake.wait_for(lock, 2s, [&] { return results.size() == 2; }));
  }
  EXPECT_TRUE(std::all_of(results.begin(), results.end(), [](const auto& result) {
    return result.error_code == ErrorCode::kShuttingDown;
  }));
}

}  // namespace
}  // namespace qingying
