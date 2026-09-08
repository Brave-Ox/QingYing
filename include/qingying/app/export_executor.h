#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

namespace qingying {

// One application-owned export worker with a bounded waiting queue. Jobs never
// depend on the UI executor; shutdown rejects queued jobs and joins the running
// one before callback targets or result leases may be destroyed.
class ExportExecutor final {
 public:
  using Task = std::function<void()>;

  explicit ExportExecutor(std::size_t max_queued);
  ~ExportExecutor();
  ExportExecutor(const ExportExecutor&) = delete;
  ExportExecutor& operator=(const ExportExecutor&) = delete;

  bool submit(Task execute, Task reject = {});
  void shutdown() noexcept;
  bool stopping() const noexcept;
  std::size_t queued() const noexcept;
  std::size_t running() const noexcept;

 private:
  struct Job {
    Task execute;
    Task reject;
  };
  void run() noexcept;

  const std::size_t max_queued_;
  std::mutex shutdown_mutex_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::queue<Job> jobs_;
  std::thread worker_;
  bool stopping_{false};
  bool running_{false};
};

}  // namespace qingying
