#include "qingying/app/action_handlers.hpp"

#include "qingying/action/i_action_handler.hpp"
#include "qingying/action/image.hpp"
#include "qingying/action/types.hpp"
#include "qingying/app/result_action_service.h"
#include "qingying/app/result_store.h"

#include <memory>
#include <utility>

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
  CaptureRegionHandler(CaptureEngine& capture, ResultStore& results,
                       CaptureRegionInvoker capture_region)
      : capture_(capture),
        results_(results),
        capture_region_(std::move(capture_region)) {}

  ActionType type() const override { return ActionType::CaptureRegion; }

  ActionResult handle(const ActionRequest& request) override {
    const auto* payload = std::get_if<CaptureRegionRequest>(&request.payload);
    if (payload == nullptr) {
      return invalidPayload("capture region");
    }

    // A new capture starts a new result lifetime. Do not retain the previous
    // full-size image when this attempt later fails.
    results_.clearScope(request.context.result_scope);

    Image image;
    ActionResult result;
    if (capture_region_) {
      result = capture_region_(request, image);
    } else {
      result = capture_.captureRegion(payload->region, image);
    }
    if (result.ok) {
      const auto id = results_.publish(request.context.result_scope,
                                      std::move(image), payload->region);
      if (id == kInvalidResultId) {
        result.ok = false;
        result.error_code = ErrorCode::kCaptureFailed;
        result.message = "capture returned an invalid image";
        result.output = std::monostate{};
      } else {
        result.output = results_.acquire(request.context.result_scope, id).metadata();
      }
    }
    return result;
  }

 private:
  CaptureEngine& capture_;
  ResultStore& results_;
  CaptureRegionInvoker capture_region_;
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
    return result_actions_.save(request.context.result_scope, payload->result,
                                payload->path);
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

void registerAppHandlers(ActionDispatcher& dispatcher, CaptureEngine& capture,
                         ResultStore& results,
                         ResultActionService& result_actions,
                         CaptureRegionInvoker capture_region) {
  dispatcher.registerHandler(std::make_unique<StatusHandler>());
  dispatcher.registerHandler(
      std::make_unique<CaptureRegionHandler>(capture, results,
                                             std::move(capture_region)));
  dispatcher.registerHandler(
      std::make_unique<CopyHandler>(results, result_actions));
  dispatcher.registerHandler(
      std::make_unique<SaveHandler>(results, result_actions));
  dispatcher.registerHandler(
      std::make_unique<PinHandler>(results, result_actions));
}

}  // namespace qingying
