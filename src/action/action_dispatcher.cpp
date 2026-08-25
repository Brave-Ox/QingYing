#include "qingying/action/action_dispatcher.hpp"

namespace qingying {

void ActionDispatcher::registerHandler(std::unique_ptr<IActionHandler> handler) {
  if (!handler) {
    return;
  }
  const Key key = static_cast<Key>(handler->type());
  handlers_[key] = std::move(handler);
}

ActionResult ActionDispatcher::dispatch(const ActionRequest& request) const {
  const Key key = static_cast<Key>(request.type);
  const auto it = handlers_.find(key);
  if (it == handlers_.end() || !it->second) {
    ActionResult r;
    r.ok = false;
    r.error_code = ErrorCode::kNotImplemented;
    r.message = "no handler registered for action";
    return r;
  }
  return it->second->handle(request);
}

}  // namespace qingying
