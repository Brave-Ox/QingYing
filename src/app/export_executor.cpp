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

bool ExportExecutor::submit(Task execute, Task reject) {
  if (!execute) return false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || jobs_.size() >= max_queued_) return false;
    jobs_.push(Job{std::move(execute), std::move(reject)});
  }
  wake_.notify_one();
  return true;
}

void ExportExecutor::shutdown() noexcept {
  std::lock_guard<std::mutex> shutdown_lock(shutdown_mutex_);
  std::vector<Task> rejected;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stopping_) stopping_ = true;
    while (!jobs_.empty()) {
      if (jobs_.front().reject) rejected.push_back(std::move(jobs_.front().reject));
      jobs_.pop();
    }
  }
  for (auto& reject : rejected) {
    try { reject(); } catch (...) {}
  }
  wake_.notify_all();
  if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
    worker_.join();
  }
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

void ExportExecutor::run() noexcept {
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
      if (stopping_ && jobs_.empty()) return;
      job = std::move(jobs_.front());
      jobs_.pop();
      running_ = true;
    }
    try { job.execute(); } catch (...) {
      if (job.reject) {
        try { job.reject(); } catch (...) {}
      }
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      running_ = false;
    }
  }
}

}  // namespace qingying
