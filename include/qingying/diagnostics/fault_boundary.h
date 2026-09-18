#pragma once

#include "qingying/diagnostics/fault_types.hpp"
#include "qingying/action/common_types.hpp"
#include <new>
#include <utility>
#include <vector>

namespace qingying {
FaultContext currentFaultContext() noexcept;
class DiagnosticScope {
 public:
  explicit DiagnosticScope(FaultContext context) noexcept;
  ~DiagnosticScope();
  DiagnosticScope(const DiagnosticScope&) = delete;
  DiagnosticScope& operator=(const DiagnosticScope&) = delete;
 private:
  FaultContext previous_;
};
// Bounded in-memory records and sampled debugger output. No exception messages,
// titles, paths, JSON arguments or image data are accepted by this interface.
FaultDiagnostic recordFault(int code, FaultOrigin origin, FaultDomain domain,
    const char* provider = nullptr, FaultContext context = currentFaultContext()) noexcept;
std::vector<FaultDiagnostic> recentFaults();

template <typename Function>
bool containFault(FaultOrigin origin, FaultDomain domain, Function&& function,
                  FaultDiagnostic* diagnostic = nullptr, const char* provider = nullptr) noexcept {
  try {
    std::forward<Function>(function)();
    return true;
  } catch (const std::bad_alloc&) {
    const auto fault = recordFault(ErrorCode::kResourceLimit, origin, domain, provider);
    if (diagnostic) *diagnostic = fault;
  } catch (...) {
    const auto fault = recordFault(ErrorCode::kUnknown, origin, domain, provider);
    if (diagnostic) *diagnostic = fault;
  }
  return false;
}
}  // namespace qingying
