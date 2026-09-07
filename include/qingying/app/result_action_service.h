#pragma once

#include "qingying/action/types.hpp"
#include "qingying/app/result_store.h"

#include <Windows.h>

#include <string>
#include <functional>
#include <optional>

namespace qingying {

class ExportService;
class PinManager;

// The single result-action boundary for GUI, Dispatcher and PinWindow.
// Callers select a result explicitly; the service owns export and save-dialog
// policy while PinManager remains responsible only for pin windows.
class ResultActionService final {
 public:
  using SaveDialog = std::function<std::optional<std::wstring>(HWND)>;
  ResultActionService(ResultStore& results, ExportService& export_service,
                      PinManager& pin_manager, SaveDialog save_dialog = {});

  void setOwnerWindow(HWND owner_window) noexcept;
  void bindPinWindowActions();

  ActionResult copy(ResultId result_id);
  ActionResult save(ResultId result_id, const std::wstring& path);
  ActionResult save(ResultId result_id);
  ActionResult pin(ResultId result_id);
  ActionResult copy(ResultScopeId scope, const ResultSelection& selection);
  ActionResult save(ResultScopeId scope, const ResultSelection& selection,
                    const std::wstring& path);
  ActionResult save(ResultScopeId scope, const ResultSelection& selection);
  ActionResult pin(ResultScopeId scope, const ResultSelection& selection);

 private:
  ActionResult copyImage(const Image& image);
  ActionResult saveImage(const Image& image, const std::wstring& path);
  ActionResult saveImageWithDialog(const Image& image,
                                   bool show_error_message);
  ActionResult pinImage(const Image& image);
  ActionResult noResult(const char* action) const;
  void showSaveUnavailableMessage() const;
  void showSaveErrorMessage() const;

  ResultStore& results_;
  ExportService& export_service_;
  PinManager& pin_manager_;
  HWND owner_window_{nullptr};
  SaveDialog save_dialog_;
};

}  // namespace qingying
