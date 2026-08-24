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
    r.message = "qingying running";
    return r;
  }
};

}  // namespace

Application::Application(HINSTANCE instance) : instance_(instance) {}

Application::~Application() = default;

void Application::registerHandlers() {
  dispatcher_.registerHandler(std::make_unique<StatusHandler>());
}

int Application::run() {
  if (!single_instance_.acquired()) {
    MessageBoxW(nullptr,
                L"QingYing is already running in the system tray.",
                L"QingYing", MB_OK | MB_ICONINFORMATION);
    return 1;
  }

  registerHandlers();

  if (!tray_.create(instance_)) {
    MessageBoxW(nullptr, L"Failed to create system tray icon.", L"QingYing",
                MB_OK | MB_ICONERROR);
    return 2;
  }

  MSG msg = {};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

}  // namespace qingying
