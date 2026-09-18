#pragma once

#include "qingying/action/automation_limits.h"
#include "qingying/automation/status_request.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace qingying {

struct ResourceUsage {
  std::uint64_t result_bytes{0};
  std::uint32_t agent_pin_count{0};
  std::uint64_t agent_pin_bytes{0};
  std::uint64_t reserved_result_bytes{0};
};

struct QueueUsage {
  std::uint32_t queued{0};
  std::uint32_t running{0};
  std::uint32_t ordinary{0};
  std::uint32_t control{0};
};

struct StatusInfo {
  bool reachable{false};
  // Absence means unknown, including when the application cannot be reached.
  std::optional<bool> app_running;
  std::optional<bool> automation_enabled;
  std::optional<bool> busy;
  std::string busy_reason;
  std::string build_version;
  std::string connection_reason;
  std::vector<std::string> capabilities;
  std::optional<AutomationLimits> limits;
  std::optional<ResourceUsage> resources;
  std::optional<QueueUsage> queues;
};

}  // namespace qingying
