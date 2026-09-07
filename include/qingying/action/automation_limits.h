#pragma once

#include <chrono>
#include <cstdint>
#include <limits>

namespace qingying {

// Shared admission and retention policy. These values describe the first
// automation profile; enforcement belongs to each resource owner.
struct AutomationLimits {
  std::uint32_t max_connections{4};
  std::uint32_t max_results_per_scope{1};
  std::chrono::milliseconds result_ttl{std::chrono::seconds{60}};
  std::uint64_t max_capture_pixels{16777216};
  std::uint64_t max_result_bytes{128ULL * 1024 * 1024};
  // Includes retained results and active consumer leases.
  std::uint64_t max_retained_result_bytes{128ULL * 1024 * 1024};
  std::uint32_t max_desktop_operations{1};
  std::uint32_t max_queued_requests_per_connection{8};
  std::uint32_t max_queued_requests_global{32};
  std::uint32_t max_completed_operations_per_connection{64};
  std::chrono::milliseconds completed_operation_ttl{std::chrono::minutes{5}};
  std::uint32_t max_agent_pins{8};
  std::uint64_t max_agent_pin_bytes{64ULL * 1024 * 1024};
  std::uint64_t max_frame_bytes{64ULL * 1024};
  std::uint32_t max_json_depth{16};
  std::chrono::milliseconds default_longshot_timeout{std::chrono::seconds{180}};

  // Initial finite defaults selected for resources whose numeric limits were
  // not fixed by the architecture plan. They remain injectable policy.
  std::chrono::milliseconds default_request_timeout{std::chrono::seconds{30}};
  std::chrono::milliseconds max_request_timeout{std::chrono::minutes{30}};
  std::uint32_t max_control_requests_per_connection{4};
  std::uint32_t max_control_requests_global{16};
  std::uint32_t max_output_frames_per_connection{16};
  std::uint64_t max_output_bytes_per_connection{1024ULL * 1024};
  std::uint32_t max_export_workers{1};
  std::uint32_t max_queued_exports{8};
  std::uint32_t max_window_query_utf16_units{1024};
  std::uint32_t max_filename_utf16_units{255};
  std::uint32_t max_path_utf16_units{32767};
  std::uint32_t max_request_key_bytes{128};
  std::uint32_t max_opaque_handle_bytes{128};
  std::uint32_t max_tombstones_per_connection{64};
  std::chrono::milliseconds tombstone_ttl{std::chrono::minutes{5}};

  constexpr bool valid() const noexcept {
    return max_connections > 0 && max_results_per_scope > 0 &&
           result_ttl.count() > 0 && max_capture_pixels > 0 &&
           max_result_bytes > 0 && max_retained_result_bytes > 0 &&
           max_desktop_operations > 0 &&
           max_queued_requests_per_connection > 0 &&
           max_queued_requests_global > 0 &&
           max_completed_operations_per_connection > 0 &&
           completed_operation_ttl.count() > 0 && max_agent_pins > 0 &&
           max_agent_pin_bytes > 0 && max_frame_bytes > 0 &&
           max_json_depth > 0 && default_longshot_timeout.count() > 0 &&
           default_request_timeout.count() > 0 &&
           max_request_timeout.count() > 0 &&
           // Keep configured deadlines bounded even before platform-specific
           // checked deadline arithmetic in the scheduler.
           max_request_timeout <= std::chrono::hours{24} &&
           default_request_timeout <= max_request_timeout &&
           default_longshot_timeout <= max_request_timeout &&
           max_control_requests_per_connection > 0 &&
           max_control_requests_global > 0 &&
           max_output_frames_per_connection > 0 &&
           max_output_bytes_per_connection > 0 && max_export_workers > 0 &&
           max_queued_exports > 0 && max_window_query_utf16_units > 0 &&
           max_filename_utf16_units > 0 && max_path_utf16_units > 0 &&
           max_request_key_bytes > 0 && max_opaque_handle_bytes > 0 &&
           max_tombstones_per_connection > 0 && tombstone_ttl.count() > 0 &&
           max_result_bytes <= max_retained_result_bytes &&
           // Captures use four bytes per physical pixel; divide before
           // comparing so even an injected UINT64_MAX cannot overflow.
           max_capture_pixels <= max_result_bytes / 4 &&
           max_queued_requests_per_connection <= max_queued_requests_global &&
           max_control_requests_per_connection <= max_control_requests_global &&
           max_frame_bytes <= max_output_bytes_per_connection &&
           max_frame_bytes <= (std::numeric_limits<std::uint32_t>::max)() &&
           max_filename_utf16_units <= max_path_utf16_units &&
           max_request_key_bytes <= max_frame_bytes &&
           max_opaque_handle_bytes <= max_frame_bytes;
  }
};

}  // namespace qingying
