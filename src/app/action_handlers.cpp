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
    if (request.width <= 0 || request.height <= 0) {
      ActionResult r;
      r.ok = false;
      r.error_code = ErrorCode::kInvalidArgument;
      r.message = "invalid capture region";
      return r;
    }

    Image image;
    ActionResult result;
    if (capture_region_) {
      result = capture_region_(request, image);
    } else {
      result = capture_.captureRegion(request.x, request.y, request.width,
                                      request.height, image);
    }
    if (result.ok &&
        results_.publish(std::move(image)) == kInvalidResultId) {
      result.ok = false;
      result.error_code = ErrorCode::kCaptureFailed;
      result.message = "capture returned an invalid image";
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

  ActionResult handle(const ActionRequest& /*request*/) override {
    return result_actions_.copy(results_.currentId());
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
    return result_actions_.save(results_.currentId(), request.save_path);
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

  ActionResult handle(const ActionRequest& /*request*/) override {
    return result_actions_.pin(results_.currentId());
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
