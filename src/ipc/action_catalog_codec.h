#pragma once
#include "qingying/automation/action_catalog.h"
#include "qingying/automation/automation_contract.h"
#include <nlohmann/json.hpp>
namespace qingying::ipc {
nlohmann::json actionInputSchema(AutomationToolKind kind, const char* argument, const AutomationLimits& limits);
AutomationRequest decodeActionArguments(AutomationToolKind kind, const char* argument,
    const nlohmann::json& arguments, RequestId id, const AutomationLimits& limits);
}
