#include "qingying/app/result_action_service.h"

#include "qingying/export/export_service.hpp"
#include "qingying/app/save_policy.h"
#include "qingying/app/export_executor.h"
#include "qingying/pin/pin_manager.hpp"

#include <Windows.h>
#include <commdlg.h>

#include <iterator>
#include <utility>

namespace qingying {
namespace {

ActionResult unavailableForExport() {
  ActionResult result;
  result.error_code = ErrorCode::kShuttingDown;
  result.message = "export executor is stopping";
  return result;
}

ActionResult savePrepared() {
  ActionResult result;
  result.ok = true;
  result.error_code = ErrorCode::kOk;
  result.message = "save prepared";
  return result;
}

ActionResult copyCancelled() {
  ActionResult result;
  result.error_code = ErrorCode::kCancelled;
  result.message = "copy cancelled before clipboard commit";
  return result;
}

ActionResult resultUnavailable(const ResultStore& results, ResultScopeId scope,
                               const ResultSelection& selection,
                               const char* action) {
  if (selection.kind != ResultSelectionKind::Explicit)
  {
    ActionResult result;
    result.error_code = ErrorCode::kNotReady;
    result.message = std::string("no capture result to ") + action;
    return result;
  }
  ActionResult result;
  result.error_code = results.resultStatus(scope, selection.result_id);
  result.message = std::string(errorCodeSymbol(result.error_code));
  return result;
}

}  // namespace

ResultActionService::ResultActionService(ResultStore& results,
                                         ExportService& export_service,
                                         PinManager& pin_manager, SaveDialog save_dialog,
                                         InteractionGate* gate,
                                         SavePolicy* save_policy,
                                         SaveTransaction save_transaction,
                                         CopyTransaction copy_transaction)
    : results_(results),
      export_service_(export_service),
      pin_manager_(pin_manager), save_dialog_(std::move(save_dialog)),
      gate_(gate ? *gate : local_gate_), save_policy_(save_policy),
      save_transaction_(std::move(save_transaction)),
      copy_transaction_(std::move(copy_transaction)) {}

void ResultActionService::setOwnerWindow(HWND owner_window) noexcept {
  owner_window_ = owner_window;
}

void ResultActionService::setExportExecutor(ExportExecutor& executor, ActionExecutor post_to_ui) {
  export_executor_ = &executor;
  post_to_ui_ = std::move(post_to_ui);
}

void ResultActionService::bindPinWindowActions() {
  pin_manager_.setActionCallbacks(
      [this](const Image& image) {
        auto guard = gate_.acquire(InteractionKind::Copy);
        return guard ? copyImage(image, kInvalidResultId) : unavailable();
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
    const InteractionGate::Guard* owner,
    CommitAuthorization authorize_commit) {
  auto guard = gate_.acquire(InteractionKind::Copy, owner);
  if (!guard) return unavailable();
  const auto lease = results_.acquire(scope, selection);
  return lease ? copyImage(*lease.image(), lease.metadata().result_id,
                           std::move(authorize_commit))
               : noResult("copy");
}
ActionResult ResultActionService::copyAdmitted(
    ResultScopeId scope, const ResultSelection& selection,
    CommitAuthorization authorize_commit) {
  const auto lease = results_.acquire(scope, selection);
  return lease ? copyImage(*lease.image(), lease.metadata().result_id,
                           std::move(authorize_commit))
               : resultUnavailable(results_, scope, selection, "copy");
}
ActionResult ResultActionService::save(
    ResultScopeId scope, const ResultSelection& selection,
    const std::wstring& path, const InteractionGate::Guard* owner,
    CommitAuthorization authorize_commit) {
  auto guard = gate_.acquire(InteractionKind::SaveDialog, owner);
  if (!guard) return unavailable();
  PreparedSave task;
  auto prepared = prepareSave(scope, selection, path,
                              std::move(authorize_commit), &task);
  return prepared.ok ? executeSave(std::move(task)) : prepared;
}

ActionResult ResultActionService::prepareSave(
    ResultScopeId scope, const ResultSelection& selection,
    const std::wstring& path, CommitAuthorization authorize_commit,
    PreparedSave* output, bool overwrite) const {
  if (output == nullptr) {
    ActionResult result;
    result.error_code = ErrorCode::kInvalidArgument;
    result.message = "prepared save output is required";
    return result;
  }
  *output = {};
  const auto lease = results_.acquire(scope, selection);
  if (!lease) return noResult("save");
  if (scope == kGuiResultScopeId) {
    output->lease = lease;
    output->absolute_path = path;
    output->overwrite = true;
    output->authorize_commit = std::move(authorize_commit);
    return savePrepared();
  }
  if (save_policy_ == nullptr) {
    ActionResult result;
    result.error_code = ErrorCode::kAccessDenied;
    result.message = "external save policy is unavailable";
    return result;
  }
  ValidatedSavePath validated;
  auto validation = save_policy_->validateFullPath(path, overwrite, &validated);
  if (!validation.ok) return validation;
  output->lease = lease;
  output->absolute_path = std::move(validated.absolute_path);
  output->overwrite = validated.overwrite;
  output->authorize_commit = std::move(authorize_commit);
  return savePrepared();
}

ActionResult ResultActionService::executeSave(PreparedSave task) {
  if (!task) {
    ActionResult result;
    result.error_code = ErrorCode::kInvalidArgument;
    result.message = "invalid prepared save";
    return result;
  }
  if (save_transaction_) {
    return save_transaction_(*task.lease.image(),
        task.lease.metadata().result_id, task.absolute_path, task.overwrite,
        std::move(task.authorize_commit));
  }
  return saveImage(*task.lease.image(), task.lease.metadata().result_id,
      task.absolute_path, task.overwrite, std::move(task.authorize_commit));
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
  return saveImageWithDialog(*lease.image(), true, lease);
}
ActionResult ResultActionService::pin(
    ResultScopeId scope, const ResultSelection& selection,
    const InteractionGate::Guard* owner) {
  auto guard = gate_.acquire(InteractionKind::Pin, owner);
  if (!guard) return unavailable();
  const auto lease = results_.acquire(scope, selection);
  return lease ? pinImage(*lease.image(), lease.metadata().result_id,
                          PinSource::Gui)
               : noResult("pin");
}
ActionResult ResultActionService::pinAdmitted(
    ResultScopeId scope, const ResultSelection& selection,
    CommitAuthorization authorize_commit) {
  const auto lease = results_.acquire(scope, selection);
  return lease ? pinImage(*lease.image(), lease.metadata().result_id,
                          PinSource::Agent, std::move(authorize_commit))
               : resultUnavailable(results_, scope, selection, "pin");
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

ActionResult ResultActionService::copyImage(
    const Image& image, ResultId result_id,
    CommitAuthorization authorize_commit) {
  // Encoding/allocation may happen elsewhere, but no clipboard mutation is
  // permitted until the operation has atomically crossed its commit boundary.
  if (authorize_commit && !authorize_commit()) return copyCancelled();
  auto result = copy_transaction_ ? copy_transaction_(image)
                                  : export_service_.copyToClipboard(image);
  if (result.ok) result.output = CopiedResult{result_id};
  return result;
}

ActionResult ResultActionService::saveImage(const Image& image,
                                            ResultId result_id,
                                            const std::wstring& path,
                                            bool overwrite,
                                            CommitAuthorization authorize_commit) {
  if (path.empty()) {
    ActionResult result;
    result.ok = false;
    result.error_code = ErrorCode::kInvalidArgument;
    result.message = "save path required";
    return result;
  }
  ExportService::PngSaveOptions options;
  options.overwrite = overwrite;
  options.authorize_commit = std::move(authorize_commit);
  auto result = export_service_.savePng(image, path, std::move(options));
  if (result.ok) {
    if (auto* saved = std::get_if<SavedResult>(&result.output)) {
      saved->result_id = result_id;
    }
  }
  return result;
}

ActionResult ResultActionService::saveImageWithDialog(
    const Image& image, bool show_error_message, ResultLease lease) {
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

  if (export_executor_ && post_to_ui_) {
    // Result-backed saves hold a lease; pin images need their own immutable snapshot.
    auto snapshot = lease ? std::shared_ptr<const Image>{}
                          : std::make_shared<const Image>(image);
    auto authorize = [executor = export_executor_] { return !executor->stopping(); };
    const bool accepted = export_executor_->submit(
        [this, lease, snapshot, path = *selected_path, authorize, show_error_message] {
      ActionResult result;
      try {
        if (export_executor_->stopping()) result = unavailableForExport();
        else if (lease) {
          PreparedSave task{lease, path, true, authorize};
          result = executeSave(std::move(task));
        } else {
          result = saveImage(*snapshot, kInvalidResultId, path, true, authorize);
        }
      } catch (...) {
        result.error_code = ErrorCode::kExportFailed;
        result.message = "save worker failed";
      }
      post_to_ui_([this, result, show_error_message] {
        if (!result.ok && show_error_message && !gate_.stopping()) showSaveErrorMessage();
      });
    }, {}, 0, "gui_save");
    if (!accepted) {
      ActionResult result;
      result.error_code = export_executor_->stopping() ? ErrorCode::kShuttingDown : ErrorCode::kResourceLimit;
      result.message = "save queue unavailable";
      return result;
    }
    auto result = savePrepared();
    result.message = "save queued";
    return result;
  }
  ActionResult result = saveImage(image, kInvalidResultId, *selected_path, true);
  if (!result.ok && show_error_message) {
    showSaveErrorMessage();
  }
  return result;
}

ActionResult ResultActionService::pinImage(
    const Image& image, ResultId result_id, PinSource source,
    CommitAuthorization authorize_commit) {
  const auto created = source == PinSource::Agent
      ? pin_manager_.showAgent(image, std::move(authorize_commit))
      : pin_manager_.showGui(image);
  if (!created) {
    ActionResult result;
    result.ok = false;
    result.error_code = created.error_code;
    result.message = created.error_code == ErrorCode::kResourceLimit
        ? "agent pin budget exceeded"
        : created.error_code == ErrorCode::kCancelled
        ? "pin cancelled before window commit"
        : "failed to create pin window";
    return result;
  }

  ActionResult result;
  result.ok = true;
  result.error_code = ErrorCode::kOk;
  result.message = "capture pinned";
  result.output = PinnedResult{result_id, created.pin_id};
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
