#include "qingying/app/application_shutdown_coordinator.h"

#include <algorithm>
#include "qingying/diagnostics/fault_boundary.h"
#include <exception>
#include <utility>

namespace qingying {

const char* applicationShutdownPhaseName(
    ApplicationShutdownPhase phase) noexcept {
  switch (phase) {
    case ApplicationShutdownPhase::StopAdmission:
      return "stop_admission";
    case ApplicationShutdownPhase::RejectNewWork:
      return "reject_new_work";
    case ApplicationShutdownPhase::CancelAndWait:
      return "cancel_and_wait";
    case ApplicationShutdownPhase::DrainAndDestroy:
      return "drain_and_destroy";
  }
  return "unknown";
}

ApplicationShutdownCoordinator::ApplicationShutdownCoordinator(
    std::vector<ApplicationShutdownStep> steps, Options options)
    : steps_(std::move(steps)), options_(std::move(options)) {
  if (options_.total_budget < std::chrono::milliseconds::zero()) {
    options_.total_budget = std::chrono::milliseconds::zero();
  }
}

ApplicationShutdownReport ApplicationShutdownCoordinator::shutdown() noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  if (started_) {
    return report_;
  }
  started_ = true;

  const auto started_at = std::chrono::steady_clock::now();
  const auto application_deadline = started_at + options_.total_budget;
  bool destructive_cleanup_blocked = false;
  for (const auto& step : steps_) {
    if (destructive_cleanup_blocked &&
        step.phase == ApplicationShutdownPhase::DrainAndDestroy) {
      break;
    }

    ApplicationShutdownDiagnostic diagnostic;
    diagnostic.phase = step.phase;
    diagnostic.participant = step.participant;
    const auto step_started = std::chrono::steady_clock::now();
    diagnostic.remaining_before =
        step_started < application_deadline
            ? std::chrono::duration_cast<std::chrono::milliseconds>(
                  application_deadline - step_started)
            : std::chrono::milliseconds::zero();
    const auto requested_budget =
        step.budget > std::chrono::milliseconds::zero()
            ? step.budget
            : diagnostic.remaining_before;
    diagnostic.budget =
        (std::min)(requested_budget, diagnostic.remaining_before);
    const auto step_deadline = step_started + diagnostic.budget;
    try {
      diagnostic.completed = !step.action || step.action(step_deadline);
      if (!diagnostic.completed) {
        diagnostic.detail = "participant did not finish before deadline";
        report_.completed = false;
        destructive_cleanup_blocked = true;
      }
    } catch (const std::exception&) {
      diagnostic.detail = "participant exception";
      recordFault(ErrorCode::kUnknown, FaultOrigin::Ui, FaultDomain::Application, step.participant.c_str());
      report_.completed = false;
      destructive_cleanup_blocked = true;
    } catch (...) {
      diagnostic.detail = "unknown exception";
      recordFault(ErrorCode::kUnknown, FaultOrigin::Ui, FaultDomain::Application, step.participant.c_str());
      report_.completed = false;
      destructive_cleanup_blocked = true;
    }

    if (step.snapshot) {
      try {
        const std::string snapshot = step.snapshot();
        if (!snapshot.empty()) {
          if (!diagnostic.detail.empty()) {
            diagnostic.detail += "; ";
          }
          diagnostic.detail += snapshot;
        }
      } catch (const std::exception&) {
        if (!diagnostic.detail.empty()) {
          diagnostic.detail += "; ";
        }
        diagnostic.detail += "snapshot: ";
        diagnostic.detail += "exception";
        recordFault(ErrorCode::kUnknown, FaultOrigin::Ui, FaultDomain::Application, step.participant.c_str());
      } catch (...) {
        if (!diagnostic.detail.empty()) {
          diagnostic.detail += "; ";
        }
        diagnostic.detail += "snapshot: unknown exception";
        recordFault(ErrorCode::kUnknown, FaultOrigin::Ui, FaultDomain::Application, step.participant.c_str());
      }
    }

    const auto now = std::chrono::steady_clock::now();
    diagnostic.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - step_started);
    diagnostic.deadline_exceeded = now >= step_deadline;
    if (diagnostic.deadline_exceeded) {
      report_.deadline_exceeded = true;
    }
    report_.diagnostics.push_back(diagnostic);
    if (options_.on_diagnostic) {
      try {
        options_.on_diagnostic(report_.diagnostics.back());
      } catch (...) {
        recordFault(ErrorCode::kUnknown, FaultOrigin::Ui, FaultDomain::Application, "shutdown_diagnostic");
        // Diagnostics must never prevent the remaining shutdown steps.
      }
    }
  }

  return report_;
}

bool ApplicationShutdownCoordinator::started() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return started_;
}

}  // namespace qingying
