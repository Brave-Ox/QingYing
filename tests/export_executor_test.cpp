#include "qingying/app/export_executor.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace qingying {

TEST(ExportExecutorTest, QueueAdmissionIsBoundedAndShutdownRejectsQueuedJobs) {
  ExportExecutor executor(1);
  std::mutex mutex;
  std::condition_variable wake;
  bool release = false;
  std::atomic_bool running{false};
  std::atomic_int rejected{0};
  ASSERT_TRUE(executor.submit([&] {
    running = true;
    std::unique_lock<std::mutex> lock(mutex);
    wake.wait(lock, [&] { return release; });
  }));
  while (!running.load()) std::this_thread::yield();
  ASSERT_TRUE(executor.submit([] {}, [&] { ++rejected; }));
  EXPECT_FALSE(executor.submit([] {}));
  EXPECT_EQ(executor.queued(), 1u);
  EXPECT_EQ(executor.running(), 1u);

  std::thread shutdown([&] { executor.shutdown(); });
  while (!executor.stopping()) std::this_thread::yield();
  EXPECT_EQ(rejected.load(), 1);
  {
    std::lock_guard<std::mutex> lock(mutex);
    release = true;
  }
  wake.notify_all();
  shutdown.join();
  EXPECT_EQ(executor.queued(), 0u);
  EXPECT_EQ(executor.running(), 0u);
  EXPECT_FALSE(executor.submit([] {}));
}

TEST(ExportExecutorTest, ThrowingJobUsesItsSingleRejectionPath) {
  ExportExecutor executor(1);
  std::mutex mutex;
  std::condition_variable wake;
  int rejected = 0;
  ASSERT_TRUE(executor.submit([] { throw 7; }, [&] {
    std::lock_guard<std::mutex> lock(mutex);
    ++rejected;
    wake.notify_all();
  }));
  std::unique_lock<std::mutex> lock(mutex);
  ASSERT_TRUE(wake.wait_for(lock, 2s, [&] { return rejected == 1; }));
  lock.unlock();
  executor.shutdown();
  EXPECT_EQ(rejected, 1);
}

TEST(ExportExecutorTest,
     JoinUntilReportsBlockedCallbackWithoutReleasingWorkerOwnership) {
  ExportExecutor executor(1);
  std::promise<void> entered;
  std::future<void> entered_future = entered.get_future();
  auto release = std::make_shared<std::promise<void>>();
  std::shared_future<void> release_future = release->get_future().share();

  ASSERT_TRUE(executor.submit([&entered, release_future] {
    entered.set_value();
    release_future.wait();
  }));
  const bool entered_ready =
      entered_future.wait_for(2s) == std::future_status::ready;
  if (!entered_ready) {
    // Keep an assertion failure from turning into an unbounded destructor
    // join when the worker has not reached the callback yet.
    release->set_value();
  }
  ASSERT_TRUE(entered_ready);

  executor.requestStop();
  const auto deadline = std::chrono::steady_clock::now() + 50ms;
  EXPECT_FALSE(executor.joinUntil(deadline));
  EXPECT_EQ(executor.running(), 1u);
  const std::string snapshot = executor.diagnosticSnapshot();
  EXPECT_NE(snapshot.find("thread=export_worker"), std::string::npos);
  EXPECT_NE(snapshot.find("queue_length=0"), std::string::npos);
  EXPECT_NE(snapshot.find("last_progress=stop_requested_during_callback"),
            std::string::npos);

  release->set_value();
  EXPECT_TRUE(executor.joinUntil(std::chrono::steady_clock::now() + 2s));
  EXPECT_EQ(executor.running(), 0u);
}

}  // namespace qingying
