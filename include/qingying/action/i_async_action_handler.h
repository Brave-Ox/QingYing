#pragma once

#include "qingying/action/i_action_handler.hpp"

#include <functional>

namespace qingying {

// Application-owned scheduling hook. The action target does not create a
// thread, depend on a thread pool, or know about UI/Pipe/Workflow machinery.
// An empty executor means that the handler may run its task inline.
using ActionTask = std::function<void()>;
using ActionExecutor = std::function<void(ActionTask)>;

// Asynchronous handlers are registered separately from synchronous handlers.
// They must eventually invoke completion exactly once; the dispatcher wraps it
// so duplicate calls are harmless. If setup throws before completion, the
// dispatcher delivers an error completion. The request is borrowed only for
// this call; a handler retaining it must copy the value.
class IAsyncActionHandler {
 public:
  virtual ~IAsyncActionHandler() = default;
  virtual ActionType type() const = 0;
  virtual void handleAsync(const ActionRequest& request,
                           ActionCompletion completion,
                           const ActionExecutor& executor) = 0;
};

}  // namespace qingying
