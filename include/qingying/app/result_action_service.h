#pragma once

#include "qingying/action/action_request.hpp"
#include "qingying/action/action_result.hpp"
#include "qingying/action/i_async_action_handler.h"
#include "qingying/app/result_store.h"
#include "qingying/app/interaction_gate.h"

#include <Windows.h>

#include <string>
#include <functional>
#include <optional>

namespace qingying {

class ExportService;
class ExportExecutor;
class PinManager;
class SavePolicy;

// The single result-action boundary for GUI, Dispatcher and PinWindow.
// Callers select a result explicitly; the service owns export and save-dialog
// policy while PinManager remains responsible only for pin windows.
class ResultActionService final {
 public:
  using SaveDialog = std::function<std::optional<std::wstring>(HWND)>;
  using CommitAuthorization = std::function<bool()>;
  using SaveTransaction = std::function<ActionResult(
      const Image&, ResultId, const std::wstring&, bool,
      CommitAuthorization)>;
  using CopyTransaction = std::function<ActionResult(const Image&)>;
  struct PreparedSave {
    ResultLease lease;
    std::wstring absolute_path;
    bool overwrite{false};
    CommitAuthorization authorize_commit;

    explicit operator bool() const noexcept {
      return static_cast<bool>(lease) && !absolute_path.empty();
    }
  };
  ResultActionService(ResultStore& results, ExportService& export_service,
                      PinManager& pin_manager, SaveDialog save_dialog = {},
                      InteractionGate* gate = nullptr,
                      SavePolicy* save_policy = nullptr,
                      SaveTransaction save_transaction = {},
                      CopyTransaction copy_transaction = {});

  void setOwnerWindow(HWND owner_window) noexcept;
  // Composition must join exports before destroying this service or UI executor.
  void setExportExecutor(ExportExecutor& executor, ActionExecutor post_to_ui);
  void bindPinWindowActions();

  ActionResult copy(ResultId result_id);
  ActionResult save(ResultId result_id, const std::wstring& path);
  ActionResult save(ResultId result_id);
  ActionResult pin(ResultId result_id);
  ActionResult copy(ResultScopeId scope, const ResultSelection& selection,
                    const InteractionGate::Guard* owner = nullptr,
                    CommitAuthorization authorize_commit = {});
  // AutomationEndpoint already owns the shared interaction admission while
  // this method acquires the scoped result and commits the clipboard write.
  ActionResult copyAdmitted(ResultScopeId scope,
                            const ResultSelection& selection,
                            CommitAuthorization authorize_commit);
  ActionResult save(ResultScopeId scope, const ResultSelection& selection,
                    const std::wstring& path,
                    const InteractionGate::Guard* owner = nullptr,
                    CommitAuthorization authorize_commit = {});
  ActionResult save(ResultScopeId scope, const ResultSelection& selection,
                    const InteractionGate::Guard* owner = nullptr);
  ActionResult pin(ResultScopeId scope, const ResultSelection& selection,
                   const InteractionGate::Guard* owner = nullptr);
  ActionResult pinAdmitted(ResultScopeId scope,
                           const ResultSelection& selection,
                           CommitAuthorization authorize_commit);
  // Pin owns this image independently of ResultStore; also guards its modal UI.
  ActionResult savePinImage(const Image& image);
  ActionResult prepareSave(ResultScopeId scope,
                           const ResultSelection& selection,
                           const std::wstring& path,
                           CommitAuthorization authorize_commit,
                           PreparedSave* output,
                           bool overwrite = false) const;
  ActionResult executeSave(PreparedSave task);

 private:
  ActionResult copyImage(const Image& image, ResultId result_id,
                         CommitAuthorization authorize_commit = {});
  ActionResult saveImage(const Image& image, ResultId result_id,
                         const std::wstring& path, bool overwrite,
                         CommitAuthorization authorize_commit = {});
  ActionResult saveImageWithDialog(const Image& image,
                                   bool show_error_message, ResultLease lease = {});
  ActionResult pinImage(const Image& image, ResultId result_id,
                        PinSource source,
                        CommitAuthorization authorize_commit = {});
  ActionResult noResult(const char* action) const;
  ActionResult unavailable() const;
  void showSaveUnavailableMessage() const;
  void showSaveErrorMessage() const;

  ResultStore& results_;
  ExportService& export_service_;
  PinManager& pin_manager_;
  HWND owner_window_{nullptr};
  SaveDialog save_dialog_;
  InteractionGate local_gate_;
  InteractionGate& gate_;
  SavePolicy* save_policy_{nullptr};
  SaveTransaction save_transaction_;
  CopyTransaction copy_transaction_;
  ExportExecutor* export_executor_{nullptr};
  ActionExecutor post_to_ui_;
};

}  // namespace qingying
