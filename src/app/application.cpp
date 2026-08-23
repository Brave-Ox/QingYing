#include "qingying/app/application.hpp"

#include "qingying/action/i_action_handler.hpp"
#include "qingying/action/types.hpp"

#include <memory>

namespace qingying {
namespace {

class StatusHandler final : public IActionHandler {
 public:
  ActionType type() const override { return ActionType::Status; }

  ActionResult handle(const ActionRequest& /*request*/) override {
    ActionResult r;
    r.ok = true;
    r.error_code = ErrorCode::kOk;
    r.message = "qingying running (stub)";
    return r;
  }
};

}  // namespace

Application::Application() = default;

Application::~Application() = default;

void Application::registerHandlers() {
  dispatcher_.registerHandler(std::make_unique<StatusHandler>());
  // P0: CaptureRegion / Copy handlers register here once engines are ready.
}

int Application::run() {
  registerHandlers();

  ActionRequest req;
  req.type = ActionType::Status;
  const ActionResult result = dispatcher_.dispatch(req);
  (void)result;

  // P0: tray + hotkey message loop will live here.
  return 0;
}

}  // namespace qingying
