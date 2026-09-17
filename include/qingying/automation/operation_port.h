#pragma once

#include "qingying/action/i_async_action_handler.h"
#include <optional>
#include <string>

namespace qingying {

struct AutomationActionDescriptor {
  ActionType type;
  std::string capability;
  bool owns_interaction{false};
};

// Application-owned execution boundary. Completion acknowledges producer cleanup.
// Implementations and borrowed dependencies must outlive the endpoint.
class AutomationOperationPort {
 public:
  virtual ~AutomationOperationPort() = default;
  virtual std::optional<AutomationActionDescriptor> describe(ActionType type) const = 0;
  virtual void submit(const ActionRequest& request, ActionCompletion completion) = 0;
  virtual void shutdown() = 0;
};

struct AutomationResultUsage {
  std::uint64_t retained_bytes{0};
  std::uint64_t reserved_bytes{0};
};

// Only lifecycle/accounting is exposed; image storage and leases stay in app.
class AutomationResultPort {
 public:
  virtual ~AutomationResultPort() = default;
  virtual AutomationResultUsage budgetSnapshot() const noexcept = 0;
  virtual int resultStatus(ResultScopeId scope, ResultId id) const noexcept = 0;
  virtual int releaseResult(ResultScopeId scope, ResultId id) noexcept = 0;
  virtual void clearScope(ResultScopeId scope) noexcept = 0;
  virtual void clearAll() noexcept = 0;
  virtual void sweep() noexcept = 0;
};
}  // namespace qingying
