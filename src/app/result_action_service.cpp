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
                                         PinManager& pin_manager)
    : results_(results),
      export_service_(export_service),
      pin_manager_(pin_manager) {}

void ResultActionService::setOwnerWindow(std::uintptr_t owner_window) noexcept {
  owner_window_ = owner_window;
}

void ResultActionService::bindPinWindowActions() {
  pin_manager_.setActionCallbacks(
      [this](const Image& image) { return copyImage(image); },
      [this](const Image& image) {
        return saveImageWithDialog(image, false);
      });
}

ActionResult ResultActionService::copy(ResultId result_id) {
  const Image* image = results_.getImage(result_id);
  return image == nullptr ? noResult("copy") : copyImage(*image);
}

ActionResult ResultActionService::save(ResultId result_id,
                                       const std::wstring& path) {
  const Image* image = results_.getImage(result_id);
  if (image == nullptr) {
    return noResult("save");
  }
  return saveImage(*image, path);
}

ActionResult ResultActionService::save(ResultId result_id) {
  const Image* image = results_.getImage(result_id);
  if (image == nullptr) {
    showSaveUnavailableMessage();
    return noResult("save");
  }
  return saveImageWithDialog(*image, true);
}

ActionResult ResultActionService::pin(ResultId result_id) {
  const Image* image = results_.getImage(result_id);
  return image == nullptr ? noResult("pin") : pinImage(*image);
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
  dialog.hwndOwner = reinterpret_cast<HWND>(owner_window_);
  dialog.lpstrFilter =
      L"PNG image (*.png)\0*.png\0All files (*.*)\0*.*\0\0";
  dialog.lpstrFile = path;
  dialog.nMaxFile = static_cast<DWORD>(std::size(path));
  dialog.lpstrDefExt = L"png";
  dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

  if (!GetSaveFileNameW(&dialog)) {
    ActionResult result;
    result.ok = true;
    result.error_code = ErrorCode::kOk;
    result.message = "save cancelled";
    return result;
  }

  ActionResult result = saveImage(image, path);
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
  MessageBoxW(reinterpret_cast<HWND>(owner_window_),
              L"There is no capture to save yet.", L"QingYing",
              MB_OK | MB_ICONINFORMATION);
}

void ResultActionService::showSaveErrorMessage() const {
  MessageBoxW(reinterpret_cast<HWND>(owner_window_),
              L"Failed to save the latest capture.", L"QingYing",
              MB_OK | MB_ICONERROR);
}

}  // namespace qingying
