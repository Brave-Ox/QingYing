#pragma once

#include "qingying/capture/capture_types.hpp"
#include "qingying/automation/status_types.hpp"
#include "qingying/window/window_types.hpp"
#include <string>
#include <variant>

namespace qingying {

using ActionOutput =
    std::variant<std::monostate, StatusInfo, CapturedResult, SavedResult,
                 CopiedResult, PinnedResult, WindowCandidates>;

struct ActionResult {
  RequestId request_id{kInvalidRequestId};
  OperationId operation_id{kInvalidOperationId};
  bool ok{false};
  int error_code{ErrorCode::kUnknown};
  std::string message;
  // Optional payload: save path / MCP return value; plain UTF-8 text, no JSON dep.
  std::string data;
  // Optional diagnostic context for failed operations. Empty/zero means the
  // producer did not expose a more precise failure location.
  std::string failure_stage;
  int failure_frame{0};
  ActionOutput output{};
};

}  // namespace qingying
