#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace qingying {

enum class ApplicationShutdownPhase {
  StopAdmission,
  RejectNewWork,
  CancelAndWait,
  DrainAndDestroy,
};

using ApplicationShutdownDeadline = std::chrono::steady_clock::time_point;

const char* applicationShutdownPhaseName(
    ApplicationShutdownPhase phase) noexcept;

// One named action in the application-level shutdown protocol. The snapshot
// is intentionally a string: each owner can expose the useful local facts
// without making the coordinator depend on every worker implementation.
struct ApplicationShutdownStep {
  ApplicationShutdownPhase phase{ApplicationShutdownPhase::StopAdmission};
  std::string participant;
  std::function<bool(ApplicationShutdownDeadline)> action;
  std::function<std::string()> snapshot;
  // Zero means this step may use the remaining application-wide budget.
  std::chrono::milliseconds budget{0};
};

struct ApplicationShutdownDiagnostic {
  ApplicationShutdownPhase phase{ApplicationShutdownPhase::StopAdmission};
  std::string participant;
  std::chrono::milliseconds elapsed{0};
  std::chrono::milliseconds budget{0};
  std::chrono::milliseconds remaining_before{0};
  bool completed{false};
  bool deadline_exceeded{false};
  std::string detail;
};

struct ApplicationShutdownReport {
  bool completed{true};
  bool deadline_exceeded{false};
  std::vector<ApplicationShutdownDiagnostic> diagnostics;
};

class ApplicationShutdownCoordinator final {
 public:
  struct Options {
    std::chrono::milliseconds total_budget{5000};
    std::function<void(const ApplicationShutdownDiagnostic&)> on_diagnostic;
  };

  ApplicationShutdownCoordinator(std::vector<ApplicationShutdownStep> steps,
                                 Options options = {});

  ApplicationShutdownCoordinator(const ApplicationShutdownCoordinator&) =
      delete;
  ApplicationShutdownCoordinator& operator=(
      const ApplicationShutdownCoordinator&) = delete;

  // Runs the protocol once. Repeated calls return the same report and do not
  // invoke a participant twice. A slow participant is never detached or
  // forcefully terminated; it is reported and allowed to finish safely.
  ApplicationShutdownReport shutdown() noexcept;

  bool started() const noexcept;

 private:
  std::vector<ApplicationShutdownStep> steps_;
  Options options_;
  mutable std::mutex mutex_;
  bool started_{false};
  ApplicationShutdownReport report_;
};

}  // namespace qingying
