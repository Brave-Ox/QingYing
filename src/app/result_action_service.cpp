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
                                         PinManager& pin_manager, SaveDialog save_dialog)
    : results_(results),
      export_service_(export_service),
      pin_manager_(pin_manager), save_dialog_(std::move(save_dialog)) {}

void ResultActionService::setOwnerWindow(HWND owner_window) noexcept {
  owner_window_ = owner_window;
}

void ResultActionService::bindPinWindowActions() {
  pin_manager_.setActionCallbacks(
      [this](const Image& image) { return copyImage(image); },
      [this](const Image& image) {
        return saveImageWithDialog(image, false);
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
    ResultScopeId scope, const ResultSelection& selection) {
  const auto lease = results_.acquire(scope, selection);
  return lease ? copyImage(*lease.image()) : noResult("copy");
}
ActionResult ResultActionService::save(
    ResultScopeId scope, const ResultSelection& selection,
    const std::wstring& path) {
  const auto lease = results_.acquire(scope, selection);
  return lease ? saveImage(*lease.image(), path) : noResult("save");
}
ActionResult ResultActionService::save(
    ResultScopeId scope, const ResultSelection& selection) {
  // Keep ownership across the nested message pump and the subsequent export.
  const auto lease = results_.acquire(scope, selection);
  if (!lease) {
    showSaveUnavailableMessage();
    return noResult("save");
  }
  return saveImageWithDialog(*lease.image(), true);
}
ActionResult ResultActionService::pin(
    ResultScopeId scope, const ResultSelection& selection) {
  const auto lease = results_.acquire(scope, selection);
  return lease ? pinImage(*lease.image()) : noResult("pin");
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
