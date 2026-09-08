#pragma once

#include "qingying/action/types.hpp"
#include "qingying/app/result_store.h"
#include "qingying/app/interaction_gate.h"

#include <Windows.h>

#include <string>
#include <functional>
#include <optional>

namespace qingying {

class ExportService;
class PinManager;
class SavePolicy;

// The single result-action boundary for GUI, Dispatcher and PinWindow.
// Callers select a result explicitly; the service owns export and save-dialog
// policy while PinManager remains responsible only for pin windows.
class ResultActionService final {
 public:
  using SaveDialog = std::function<std::optional<std::wstring>(HWND)>;
  using CommitAuthorization = std::function<bool()>;
  ResultActionService(ResultStore& results, ExportService& export_service,
                      PinManager& pin_manager, SaveDialog save_dialog = {},
                      InteractionGate* gate = nullptr,
                      SavePolicy* save_policy = nullptr);

  void setOwnerWindow(HWND owner_window) noexcept;
  void bindPinWindowActions();

  ActionResult copy(ResultId result_id);
  ActionResult save(ResultId result_id, const std::wstring& path);
  ActionResult save(ResultId result_id);
  ActionResult pin(ResultId result_id);
  ActionResult copy(ResultScopeId scope, const ResultSelection& selection,
                    const InteractionGate::Guard* owner = nullptr);
  ActionResult save(ResultScopeId scope, const ResultSelection& selection,
                    const std::wstring& path,
                    const InteractionGate::Guard* owner = nullptr,
                    CommitAuthorization authorize_commit = {});
  ActionResult save(ResultScopeId scope, const ResultSelection& selection,
                    const InteractionGate::Guard* owner = nullptr);
  ActionResult pin(ResultScopeId scope, const ResultSelection& selection,
                   const InteractionGate::Guard* owner = nullptr);
  // Pin owns this image independently of ResultStore; also guards its modal UI.
  ActionResult savePinImage(const Image& image);

 private:
  ActionResult copyImage(const Image& image);
  ActionResult saveImage(const Image& image, ResultId result_id,
                         const std::wstring& path, bool overwrite,
                         CommitAuthorization authorize_commit = {});
  ActionResult saveImageWithDialog(const Image& image,
                                   bool show_error_message);
  ActionResult pinImage(const Image& image);
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
};

}  // namespace qingying
