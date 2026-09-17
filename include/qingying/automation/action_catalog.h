#pragma once
#include "qingying/automation/operation_port.h"
#include <array>
#include <vector>

namespace qingying {
// Schema/decoder selectors belong to the product contract, not a transport.
enum class AutomationToolKind { Status, CaptureWindow, CropCenter, Copy, Save, Pin,
  GetOperation, CancelOperation, ReleaseResult };
enum class AutomationResultKind { Status, Image, Copied, Saved, Pinned, Operation, Cancellation, Released };
struct ActionDescriptor {
  AutomationToolKind kind;
  const char* id;
  const char* description;
  const char* handle_argument;
  std::optional<ActionType> action;
  bool owns_interaction;
  bool asynchronous;
  bool cancellable;
  AutomationResultKind result;
  // All action payloads use the same typed validator in every entry point.
  ActionValidationResult (*validate)(const ActionRequest&);
};
inline const std::array<ActionDescriptor, 9>& actionCatalog() {
  static const std::array<ActionDescriptor, 9> catalog{{
    {AutomationToolKind::Status, "status", "Query desktop automation availability and limits", "", ActionType::Status, false, false, false, AutomationResultKind::Status, validateActionRequest},
    {AutomationToolKind::CaptureWindow, "capture_window", "Capture visible desktop pixels for one matching window", "", ActionType::CaptureWindow, true, true, true, AutomationResultKind::Image, validateActionRequest},
    {AutomationToolKind::CropCenter, "crop_center", "Capture the physical center of the primary display", "", ActionType::CropCenter, true, true, true, AutomationResultKind::Image, validateActionRequest},
    {AutomationToolKind::Copy, "copy", "Copy a connection-owned result to the clipboard", "", ActionType::Copy, false, false, true, AutomationResultKind::Copied, validateActionRequest},
    {AutomationToolKind::Save, "save", "Save a connection-owned result as a PNG file", "", ActionType::Save, true, true, true, AutomationResultKind::Saved, validateActionRequest},
    {AutomationToolKind::Pin, "pin", "Create a persistent topmost window from a connection-owned result", "", ActionType::Pin, false, false, true, AutomationResultKind::Pinned, validateActionRequest},
    {AutomationToolKind::GetOperation, "get_operation", "Query a connection-owned operation", "operation_id", {}, false, false, false, AutomationResultKind::Operation, nullptr},
    {AutomationToolKind::CancelOperation, "cancel_operation", "Request cancellation of a connection-owned operation", "operation_id", {}, false, false, false, AutomationResultKind::Cancellation, nullptr},
    {AutomationToolKind::ReleaseResult, "release_result", "Release a connection-owned result", "result_id", {}, false, false, false, AutomationResultKind::Released, nullptr}}};
  return catalog;
}
inline const ActionDescriptor* findActionDescriptor(std::string_view id) {
  for (const auto& entry : actionCatalog()) if (id == entry.id) return &entry;
  return nullptr;
}
inline const char* automationActionId(ActionType type) {
  for (const auto& entry : actionCatalog()) if (entry.action == type) return entry.id;
  // Reserved wire v1 actions have no external handler/descriptor yet.
  if (type == ActionType::CaptureRegion) return "capture_region";
  if (type == ActionType::LongShotRegion) return "longshot_select";
  return "";
}
inline std::vector<AutomationActionDescriptor> automationActionDescriptors() {
  std::vector<AutomationActionDescriptor> result;
  for (const auto& entry : actionCatalog())
    if (entry.action && *entry.action != ActionType::Status)
      result.push_back({*entry.action, entry.id, entry.owns_interaction});
  return result;
}
inline std::vector<ActionType> automationReadyActions() {
  std::vector<ActionType> result;
  for (const auto& entry : automationActionDescriptors()) result.push_back(entry.type);
  return result;
}
}  // namespace qingying
