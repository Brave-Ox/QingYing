#pragma once

#include <condition_variable>
#include "qingying/diagnostics/fault_types.hpp"
#include "qingying/action/image_memory_budget.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace qingying {

// One application-owned export worker with a bounded waiting queue. Jobs never
// depend on the UI executor; shutdown rejects queued jobs and joins the running
// one before callback targets or result leases may be destroyed.
class ExportExecutor final {
 public:
  using Task = std::function<void()>;

  explicit ExportExecutor(std::size_t max_queued,
                          std::string thread_name = "export_worker");
  ~ExportExecutor();
  ExportExecutor(const ExportExecutor&) = delete;
  ExportExecutor& operator=(const ExportExecutor&) = delete;

  bool submit(Task execute, Task reject = {}, std::uint64_t request_id = 0,
              std::string operation = {});
  // Signals cancellation and rejects queued work without waiting for the
  // currently running export callback.
  void requestStop() noexcept;
  // Waits for the worker to finish until the supplied monotonic deadline.
  // A false result leaves the worker and its context owned by this object.
  bool joinUntil(std::chrono::steady_clock::time_point deadline) noexcept;
  void shutdown() noexcept;
  bool stopping() const noexcept;
  std::size_t queued() const noexcept;
  std::size_t running() const noexcept;
  std::string diagnosticSnapshot() const;

 private:
  struct Job {
    ImageMemoryBudget::Token queue_memory;
    Task execute;
    Task reject;
    std::uint64_t request_id{0};
    std::string operation;
    FaultContext diagnostic_context;
  };
  void run() noexcept;

  const std::size_t max_queued_;
  const std::string thread_name_;
  std::mutex shutdown_mutex_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable done_;
  std::queue<Job> jobs_;
  std::thread worker_;
  bool stopping_{false};
  bool running_{false};
  bool done_state_{false};
  std::uint64_t active_request_id_{0};
  std::string active_operation_;
  std::string last_progress_{"worker_started"};
};

}  // namespace qingying
