#include "qingying/app/export_executor.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
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

}  // namespace qingying
