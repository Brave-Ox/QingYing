#pragma once

#include "qingying/action/types.hpp"

namespace qingying {

class IActionHandler {
 public:
  virtual ~IActionHandler() = default;
  virtual ActionType type() const = 0;
  virtual ActionResult handle(const ActionRequest& request) = 0;
};

}  // namespace qingying
