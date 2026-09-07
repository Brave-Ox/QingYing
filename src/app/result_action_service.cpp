#include "qingying/app/result_action_service.h"

#include "qingying/export/export_service.hpp"
#include "qingying/pin/pin_manager.hpp"

#include <Windows.h>
#include <commdlg.h>

#include <iterator>
#include <utility>

namespace qingying {

ResultActionService::ResultActionService(ResultStore& results,
                                         ExportService& export_service,
                                         PinManager& pin_manager, SaveDialog save_dialog,
                                         InteractionGate* gate)
    : results_(results),
      export_service_(export_service),
      pin_manager_(pin_manager), save_dialog_(std::move(save_dialog)),
      gate_(gate ? *gate : local_gate_) {}

void ResultActionService::setOwnerWindow(HWND owner_window) noexcept {
  owner_window_ = owner_window;
}

void ResultActionService::bindPinWindowActions() {
  pin_manager_.setActionCallbacks(
      [this](const Image& image) {
        auto guard = gate_.acquire(InteractionKind::Copy);
        return guard ? copyImage(image) : unavailable();
      },
      [this](const Image& image) {
        return savePinImage(image);
      });
}

ActionResult ResultActionService::copy(ResultId id) {
  return copy(kGuiResultScopeId, ResultSelection::specific(id));
}
ActionResult ResultActionService::save(ResultId id, const std::wstring& path) {
  return save(kGuiResultScopeId, ResultSelection::specific(id), path);
}
ActionResult ResultActionService::save(ResultId id) {
  return save(kGuiResultScopeId, ResultSelection::specific(id));
}
ActionResult ResultActionService::pin(ResultId id) {
  return pin(kGuiResultScopeId, ResultSelection::specific(id));
}
ActionResult ResultActionService::copy(
    ResultScopeId scope, const ResultSelection& selection,
    const InteractionGate::Guard* owner) {
  auto guard = gate_.acquire(InteractionKind::Copy, owner);
  if (!guard) return unavailable();
  const auto lease = results_.acquire(scope, selection);
  return lease ? copyImage(*lease.image()) : noResult("copy");
}
ActionResult ResultActionService::save(
    ResultScopeId scope, const ResultSelection& selection,
    const std::wstring& path, const InteractionGate::Guard* owner) {
  auto guard = gate_.acquire(InteractionKind::SaveDialog, owner);
  if (!guard) return unavailable();
  const auto lease = results_.acquire(scope, selection);
  return lease ? saveImage(*lease.image(), path) : noResult("save");
}
ActionResult ResultActionService::save(
    ResultScopeId scope, const ResultSelection& selection,
    const InteractionGate::Guard* owner) {
  auto guard = gate_.acquire(InteractionKind::SaveDialog, owner);
  if (!guard) return unavailable();
  // Keep ownership across the nested message pump and the subsequent export.
  const auto lease = results_.acquire(scope, selection);
  if (!lease) {
    showSaveUnavailableMessage();
    return noResult("save");
  }
  return saveImageWithDialog(*lease.image(), true);
}
ActionResult ResultActionService::pin(
    ResultScopeId scope, const ResultSelection& selection,
    const InteractionGate::Guard* owner) {
  auto guard = gate_.acquire(InteractionKind::Pin, owner);
  if (!guard) return unavailable();
  const auto lease = results_.acquire(scope, selection);
  return lease ? pinImage(*lease.image()) : noResult("pin");
}

ActionResult ResultActionService::savePinImage(const Image& image) {
  auto guard = gate_.acquire(InteractionKind::SaveDialog);
  if (!guard) return unavailable();
  // A nested message pump may close the owning Pin window.
  const Image snapshot = image;
  return saveImageWithDialog(snapshot, false);
}

ActionResult ResultActionService::unavailable() const {
  ActionResult result;
  result.error_code = gate_.stopping() ? ErrorCode::kShuttingDown : ErrorCode::kBusy;
  result.message = std::string(errorCodeSymbol(result.error_code));
  return result;
}

ActionResult ResultActionService::copyImage(const Image& image) {
  return export_service_.copyToClipboard(image);
}

ActionResult ResultActionService::saveImage(const Image& image,
                                            const std::wstring& path) {
  if (path.empty()) {
    ActionResult result;
    result.ok = false;
    result.error_code = ErrorCode::kInvalidArgument;
    result.message = "save path required";
    return result;
  }
  return export_service_.savePng(image, path);
}

ActionResult ResultActionService::saveImageWithDialog(
    const Image& image, bool show_error_message) {
  if (image.empty()) {
    ActionResult result = noResult("save");
    if (show_error_message) {
      showSaveUnavailableMessage();
    }
    return result;
  }

  wchar_t path[MAX_PATH] = L"qingying.png";
  OPENFILENAMEW dialog = {};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = owner_window_;
  dialog.lpstrFilter =
      L"PNG image (*.png)\0*.png\0All files (*.*)\0*.*\0\0";
  dialog.lpstrFile = path;
  dialog.nMaxFile = static_cast<DWORD>(std::size(path));
  dialog.lpstrDefExt = L"png";
  dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

  const auto selected_path = save_dialog_ ? save_dialog_(owner_window_)
      : (GetSaveFileNameW(&dialog) ? std::optional<std::wstring>{path}
                                   : std::nullopt);
  if (gate_.stopping()) return unavailable();
  if (!selected_path) {
    ActionResult result;
    result.ok = true;
    result.error_code = ErrorCode::kOk;
    result.message = "save cancelled";
    return result;
  }

  ActionResult result = saveImage(image, *selected_path);
  if (!result.ok && show_error_message) {
    showSaveErrorMessage();
  }
  return result;
}

ActionResult ResultActionService::pinImage(const Image& image) {
  if (!pin_manager_.show(image)) {
    ActionResult result;
    result.ok = false;
    result.error_code = ErrorCode::kUnknown;
    result.message = "failed to create pin window";
    return result;
  }

  ActionResult result;
  result.ok = true;
  result.error_code = ErrorCode::kOk;
  result.message = "capture pinned";
  return result;
}

ActionResult ResultActionService::noResult(const char* action) const {
  ActionResult result;
  result.ok = false;
  result.error_code = ErrorCode::kNotReady;
  result.message = "no capture result to ";
  result.message += action;
  return result;
}

void ResultActionService::showSaveUnavailableMessage() const {
  MessageBoxW(owner_window_,
              L"There is no capture to save yet.", L"QingYing",
              MB_OK | MB_ICONINFORMATION);
}

void ResultActionService::showSaveErrorMessage() const {
  MessageBoxW(owner_window_,
              L"Failed to save the latest capture.", L"QingYing",
              MB_OK | MB_ICONERROR);
}

}  // namespace qingying
