#pragma once

#include "qingying/action/action_request.hpp"
#include "qingying/action/action_result.hpp"

#include <functional>

namespace qingying {

// The dispatcher owns correlation and exactly-once completion; handlers only
// produce a typed ActionResult. The callback may be invoked synchronously.
using ActionCompletion = std::function<void(ActionResult)>;

class IActionHandler {
 public:
  virtual ~IActionHandler() = default;
  virtual ActionType type() const = 0;
  virtual ActionResult handle(const ActionRequest& request) = 0;
};

}  // namespace qingying
