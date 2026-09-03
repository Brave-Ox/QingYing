#include "qingying/app/action_handlers.hpp"

#include "qingying/action/i_action_handler.hpp"
#include "qingying/action/image.hpp"
#include "qingying/action/types.hpp"

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
  CaptureRegionHandler(CaptureEngine& capture, CaptureSession& session,
                       CaptureRegionInvoker capture_region)
      : capture_(capture),
        session_(session),
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
    if (result.ok) {
      session_.setResult(std::move(image));
    }
    return result;
  }

 private:
  CaptureEngine& capture_;
  CaptureSession& session_;
  CaptureRegionInvoker capture_region_;
};

class CopyHandler final : public IActionHandler {
 public:
  explicit CopyHandler(ExportService& export_service, CaptureSession& session)
      : export_service_(export_service), session_(session) {}

  ActionType type() const override { return ActionType::Copy; }

  ActionResult handle(const ActionRequest& /*request*/) override {
    if (!session_.hasResult()) {
      ActionResult r;
      r.ok = false;
      r.error_code = ErrorCode::kNotReady;
      r.message = "no capture result";
      return r;
    }
    return export_service_.copyToClipboard(session_.result());
  }

 private:
  ExportService& export_service_;
  CaptureSession& session_;
};

class SaveHandler final : public IActionHandler {
 public:
  explicit SaveHandler(ExportService& export_service, CaptureSession& session)
      : export_service_(export_service), session_(session) {}

  ActionType type() const override { return ActionType::Save; }

  ActionResult handle(const ActionRequest& request) override {
    if (!session_.hasResult()) {
      ActionResult r;
      r.ok = false;
      r.error_code = ErrorCode::kNotReady;
      r.message = "no capture result";
      return r;
    }
    if (request.save_path.empty()) {
      ActionResult r;
      r.ok = false;
      r.error_code = ErrorCode::kInvalidArgument;
      r.message = "save path required";
      return r;
    }
    return export_service_.savePng(session_.result(), request.save_path);
  }

 private:
  ExportService& export_service_;
  CaptureSession& session_;
};

class PinHandler final : public IActionHandler {
 public:
  explicit PinHandler(PinManager& pin_manager, CaptureSession& session)
      : pin_manager_(pin_manager), session_(session) {}

  ActionType type() const override { return ActionType::Pin; }

  ActionResult handle(const ActionRequest& /*request*/) override {
    if (!session_.hasResult()) {
      ActionResult r;
      r.ok = false;
      r.error_code = ErrorCode::kNotReady;
      r.message = "no capture result";
      return r;
    }

    if (!pin_manager_.show(session_.result())) {
      ActionResult r;
      r.ok = false;
      r.error_code = ErrorCode::kUnknown;
      r.message = "failed to create pin window";
      return r;
    }

    ActionResult r;
    r.ok = true;
    r.error_code = ErrorCode::kOk;
    r.message = "capture pinned";
    return r;
  }

 private:
  PinManager& pin_manager_;
  CaptureSession& session_;
};

}  // namespace

void registerAppHandlers(ActionDispatcher& dispatcher, CaptureEngine& capture,
                         ExportService& export_service, CaptureSession& session,
                         PinManager& pin_manager,
                         CaptureRegionInvoker capture_region) {
  dispatcher.registerHandler(std::make_unique<StatusHandler>());
  dispatcher.registerHandler(
      std::make_unique<CaptureRegionHandler>(capture, session,
                                             std::move(capture_region)));
  dispatcher.registerHandler(
      std::make_unique<CopyHandler>(export_service, session));
  dispatcher.registerHandler(
      std::make_unique<SaveHandler>(export_service, session));
  dispatcher.registerHandler(std::make_unique<PinHandler>(pin_manager, session));
}

}  // namespace qingying
