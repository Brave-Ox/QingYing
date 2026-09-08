#pragma once
#include "qingying/automation/automation_contract.h"
#include <nlohmann/json.hpp>
#include <array>
namespace qingying::mcp {
using Json = nlohmann::json;
enum class ToolKind { Status, GetOperation, CancelOperation, ReleaseResult };
struct Tool {
  const char* name;
  const char* description;
  const char* argument;
  ToolKind kind;
  Json descriptor(const AutomationLimits& limits) const;
  AutomationRequest decode(const Json& arguments, RequestId id, const AutomationLimits& limits) const;
  Json encode(const AutomationResponse& response, const Json& arguments, const AutomationLimits& limits) const;
};
const std::array<Tool, 4>& tools();
const Tool* findTool(const std::string& name);
Json toolFailure(int code, const std::string& message);
}  // namespace qingying::mcp
