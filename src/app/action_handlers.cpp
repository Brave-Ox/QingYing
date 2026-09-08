#include "qingying/app/action_handlers.hpp"

#include "qingying/action/i_action_handler.hpp"
#include "qingying/action/image.hpp"
#include "qingying/action/types.hpp"
#include "qingying/app/capture_service.h"
#include "qingying/app/result_action_service.h"
#include "qingying/app/result_store.h"
#include "qingying/automation/automation_contract.h"

#include <memory>

namespace qingying {
namespace {

ActionResult invalidPayload(const char* action) {
  ActionResult result;
  result.ok = false;
  result.error_code = ErrorCode::kInvalidArgument;
  result.message = "invalid payload for ";
  result.message += action;
  return result;
}

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

class CaptureRegionHandler final : public IActionHandler {
 public:
  explicit CaptureRegionHandler(CaptureService& capture_service)
      : capture_service_(capture_service) {}

  ActionType type() const override { return ActionType::CaptureRegion; }

  ActionResult handle(const ActionRequest& request) override {
    const auto* payload = std::get_if<CaptureRegionRequest>(&request.payload);
    if (payload == nullptr) {
      return invalidPayload("capture region");
    }

    return capture_service_.capture(request, payload->region);
  }

 private:
  CaptureService& capture_service_;
};

class CropCenterHandler final : public IActionHandler {
 public:
  explicit CropCenterHandler(CaptureService& capture_service)
      : capture_service_(capture_service) {}

  ActionType type() const override { return ActionType::CropCenter; }

  ActionResult handle(const ActionRequest& request) override {
    const auto* payload = std::get_if<CropCenterRequest>(&request.payload);
    if (payload == nullptr) return invalidPayload("crop center");
    return capture_service_.cropCenter(request, payload->width,
                                       payload->height);
  }

 private:
  CaptureService& capture_service_;
};

class CopyHandler final : public IActionHandler {
 public:
  explicit CopyHandler(ResultStore& results, ResultActionService& result_actions)
      : results_(results), result_actions_(result_actions) {}

  ActionType type() const override { return ActionType::Copy; }

  ActionResult handle(const ActionRequest& request) override {
    const auto* payload = std::get_if<CopyRequest>(&request.payload);
    if (payload == nullptr) {
      return invalidPayload("copy");
    }
    return result_actions_.copy(request.context.result_scope, payload->result);
  }

 private:
  ResultStore& results_;
  ResultActionService& result_actions_;
};

class SaveHandler final : public IActionHandler {
 public:
  explicit SaveHandler(ResultStore& results, ResultActionService& result_actions)
      : results_(results), result_actions_(result_actions) {}

  ActionType type() const override { return ActionType::Save; }

  ActionResult handle(const ActionRequest& request) override {
    const auto* payload = std::get_if<SaveRequest>(&request.payload);
    if (payload == nullptr) {
      return invalidPayload("save");
    }
    ResultActionService::CommitAuthorization authorize_commit;
    if (request.operation_control) {
      authorize_commit = [control = request.operation_control] {
        return control->tryCommit();
      };
    }
    return result_actions_.save(request.context.result_scope, payload->result,
                                payload->path, nullptr,
                                std::move(authorize_commit));
  }

 private:
  ResultStore& results_;
  ResultActionService& result_actions_;
};

class PinHandler final : public IActionHandler {
 public:
  explicit PinHandler(ResultStore& results, ResultActionService& result_actions)
      : results_(results), result_actions_(result_actions) {}

  ActionType type() const override { return ActionType::Pin; }

  ActionResult handle(const ActionRequest& request) override {
    const auto* payload = std::get_if<PinRequest>(&request.payload);
    if (payload == nullptr) {
      return invalidPayload("pin");
    }
    return result_actions_.pin(request.context.result_scope, payload->result);
  }

 private:
  ResultStore& results_;
  ResultActionService& result_actions_;
};

}  // namespace

void registerAppHandlers(ActionDispatcher& dispatcher,
                         CaptureService& capture_service,
                         ResultStore& results,
                         ResultActionService& result_actions) {
  dispatcher.registerHandler(std::make_unique<StatusHandler>());
  dispatcher.registerHandler(
      std::make_unique<CaptureRegionHandler>(capture_service));
  dispatcher.registerHandler(
      std::make_unique<CropCenterHandler>(capture_service));
  dispatcher.registerHandler(
      std::make_unique<CopyHandler>(results, result_actions));
  dispatcher.registerHandler(
      std::make_unique<SaveHandler>(results, result_actions));
  dispatcher.registerHandler(
      std::make_unique<PinHandler>(results, result_actions));
}

}  // namespace qingying
