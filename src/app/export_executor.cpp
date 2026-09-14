#include "qingying/app/export_executor.h"

#include <stdexcept>
#include <utility>
#include <vector>

namespace qingying {

ExportExecutor::ExportExecutor(std::size_t max_queued)
    : max_queued_(max_queued) {
  if (max_queued_ == 0) throw std::invalid_argument("export queue capacity");
  worker_ = std::thread([this] { run(); });
}

ExportExecutor::~ExportExecutor() { shutdown(); }

bool ExportExecutor::submit(Task execute, Task reject,
                            std::uint64_t request_id,
                            std::string operation) {
  if (!execute) return false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || jobs_.size() >= max_queued_) return false;
    jobs_.push(Job{std::move(execute), std::move(reject), request_id,
                   std::move(operation)});
    last_progress_ = "queued";
  }
  wake_.notify_one();
  return true;
}

void ExportExecutor::shutdown() noexcept {
  requestStop();
  (void)joinUntil((std::chrono::steady_clock::time_point::max)());
}

void ExportExecutor::requestStop() noexcept {
  std::vector<Task> rejected;
  {
    std::lock_guard<std::mutex> shutdown_lock(shutdown_mutex_);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!stopping_) stopping_ = true;
      while (!jobs_.empty()) {
        if (jobs_.front().reject) {
          rejected.push_back(std::move(jobs_.front().reject));
        }
        jobs_.pop();
      }
      last_progress_ = running_ ? "stop_requested_during_callback"
                                : "queued_work_rejected";
    }
  }
  for (auto& reject : rejected) {
    try { reject(); } catch (...) {}
  }
  wake_.notify_all();
}

bool ExportExecutor::joinUntil(
    std::chrono::steady_clock::time_point deadline) noexcept {
  std::unique_lock<std::mutex> shutdown_lock(shutdown_mutex_);
  if (!worker_.joinable()) return true;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!done_state_ &&
        done_.wait_until(lock, deadline) == std::cv_status::timeout &&
        !done_state_) {
      return false;
    }
  }
  if (worker_.get_id() == std::this_thread::get_id()) return false;
  worker_.join();
  return true;
}

bool ExportExecutor::stopping() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return stopping_;
}

std::size_t ExportExecutor::queued() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return jobs_.size();
}

std::size_t ExportExecutor::running() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return running_ ? 1u : 0u;
}

std::string ExportExecutor::diagnosticSnapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return "thread=export_worker request_id=" +
         std::to_string(active_request_id_) + " operation=" +
         (active_operation_.empty() ? std::string("none")
                                    : active_operation_) +
         " queue_length=" + std::to_string(jobs_.size()) +
         " last_progress=" + last_progress_;
}

void ExportExecutor::run() noexcept {
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
      if (stopping_ && jobs_.empty()) {
        done_state_ = true;
        lock.unlock();
        done_.notify_all();
        return;
      }
      job = std::move(jobs_.front());
      jobs_.pop();
      running_ = true;
      active_request_id_ = job.request_id;
      active_operation_ = job.operation;
      last_progress_ = "callback_started";
    }
    try { job.execute(); } catch (...) {
      if (job.reject) {
        try { job.reject(); } catch (...) {}
      }
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      running_ = false;
      active_request_id_ = 0;
      active_operation_.clear();
      last_progress_ = "callback_finished";
    }
  }
}

}  // namespace qingying
