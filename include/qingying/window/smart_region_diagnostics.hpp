#pragma once

#include "qingying/window/smart_region_types.hpp"

namespace qingying {

constexpr std::size_t SmartRegionDiagnosticMaxCandidates =
    SmartRegionMaxCandidates;
constexpr std::size_t SmartRegionDiagnosticWindowClassCapacity = 128;
constexpr std::size_t SmartRegionDiagnosticProcessNameCapacity = 260;

// MSAA 后台查询的最后一个有效遍历路径与停止原因。仅用于诊断，不参与候选排序。
enum class SmartRegionMsaaTraversalPath : std::uint8_t {
  None,
  DirectPoint,
  RootHitTest,
  AccessibleChildren,
};

enum class SmartRegionMsaaTraversalStopReason : std::uint8_t {
  None,
  CandidateFound,
  NoChildren,
  HitTestFailed,
  NodeBudgetExhausted,
  TimeBudgetExhausted,
  DepthLimitReached,
  NoCandidate,
};

// MSAA 命中节点未形成候选时的明确过滤原因。仅诊断使用，不参与候选排序。
enum class SmartRegionMsaaFilteredNodeReason : std::uint8_t {
  None,
  MissingAccessible,
  RoleUnavailable,
  RectUnavailable,
  UnknownSemantic,
  OutsideOwner,
  WindowSizedContentSurface,
  InvisibleOrOffscreen,
  PointerOutside,
};

// 记录最后一个被候选规则拒绝的 MSAA 命中节点，便于定位浏览器控件的
// role/state/坐标与过滤规则；默认值表示本次未记录到被拒绝节点。
struct SmartRegionMsaaFilteredNodeDiagnostic {
  SmartRegionMsaaFilteredNodeReason reason{
      SmartRegionMsaaFilteredNodeReason::None};
  WindowRect rect;
  LONG role{0};
  LONG state{0};
  std::uint8_t accessibility_depth{0};
};

struct SmartRegionMsaaTraversalDiagnostic {
  SmartRegionMsaaTraversalPath path{SmartRegionMsaaTraversalPath::None};
  SmartRegionMsaaTraversalStopReason stop_reason{
      SmartRegionMsaaTraversalStopReason::None};
  std::size_t visited_child_count{0};
  SmartRegionMsaaFilteredNodeDiagnostic filtered_node;
};

// 候选未参与最终智能吸附的明确原因。诊断开启时用于定位规则误判，
// 不参与候选排序，也不改变原有选择结果。
enum class SmartRegionCandidateRejection : std::uint8_t {
  None,
  Invalid,
  PointerOutside,
  OutsideOwner,
  TooSmall,
  LowConfidence,
  GenericTooLarge,
  Duplicate,
  LowerScore,
};

// 仅用于诊断：当前异步结果是否具备未来“快速移动时延后显示”策略的条件。
// 该枚举不改变候选选择、异步查询或覆盖层渲染行为。
enum class SmartRegionAsyncDeferralReason : std::uint8_t {
  NotEvaluated,
  NotChromiumBrowser,
  MotionBelowThreshold,
  FastMotion,
};

// 单个候选的诊断快照；固定容量，避免在鼠标移动路径上额外分配内存。
struct SmartRegionCandidateDiagnostic {
  SmartRegionCandidate candidate;
  int score{0};
  int source_score{0};
  int semantic_score{0};
  int pointer_score{0};
  int area_score{0};
  int quality_score{0};
  int boundary_score{0};
  int hierarchy_score{0};
  std::int64_t area{0};
  std::uint8_t owner_coverage_percent{0};
  SmartRegionCandidateRejection rejection{
      SmartRegionCandidateRejection::None};
  bool selected{false};
};

// 单次候选检测的轻量诊断记录。仅在调用方显式启用时保存。
struct SmartRegionDiagnosticEvent {
  SmartRegionDiagnosticSource source{SmartRegionDiagnosticSource::None};
  WindowRect rect;
  std::uintptr_t root_window{0};
  int cursor_x{0};
  int cursor_y{0};
  std::uint32_t process_id{0};
  wchar_t window_class[SmartRegionDiagnosticWindowClassCapacity]{};
  wchar_t process_name[SmartRegionDiagnosticProcessNameCapacity]{};
  std::uint64_t elapsed_ms{0};
  std::uint64_t window_detection_ms{0};
  std::uint64_t uia_lookup_ms{0};
  std::uint64_t msaa_lookup_ms{0};
  std::uint64_t known_content_lookup_ms{0};
  std::uint64_t visual_lookup_ms{0};
  std::uint64_t window_snapshot_cache_age_ms{0};
  std::uint64_t selection_ms{0};
  std::uint64_t overlay_render_ms{0};
  std::uint64_t overlay_render_count{0};
  std::uint64_t hover_input_delay_ms{0};
  std::uint64_t stabilization_delay_ms{0};
  std::int64_t hover_motion_delta_x{0};
  std::int64_t hover_motion_delta_y{0};
  std::uint64_t hover_motion_elapsed_ms{0};
  std::uint64_t uia_async_request_id{0};
  std::uint64_t uia_async_elapsed_ms{0};
  std::uint64_t uia_async_age_ms{0};
  std::uint64_t uia_async_poll_delay_ms{0};
  std::size_t uia_async_candidate_count{0};
  std::uint8_t visual_edge_mask{0};
  std::uint8_t visual_candidate_confidence{0};
  bool window_detection_attempted{false};
  bool window_snapshot_cache_lookup_attempted{false};
  bool window_snapshot_cache_hit{false};
  bool uia_lookup_attempted{false};
  bool uia_has_valid_local_candidate{false};
  bool msaa_lookup_attempted{false};
  bool msaa_candidate_found{false};
  bool known_content_lookup_attempted{false};
  bool visual_lookup_attempted{false};
  bool visual_cache_hit{false};
  bool visual_cache_contains_candidate{false};
  bool browser_shell_policy_active{false};
  bool uia_async_result_received{false};
  bool uia_async_result_succeeded{false};
  bool uia_async_msaa_attempted{false};
  bool uia_async_browser_semantic_miss{false};
  bool uia_async_cache_hit{false};
  bool uia_async_suppressed_by_cooldown{false};
  bool uia_async_matches_current_request{false};
  bool uia_async_result_applied{false};
  bool uia_async_result_deferred{false};
  bool hover_motion_fast{false};
  SmartRegionAsyncDeferralReason uia_async_deferral_reason{
      SmartRegionAsyncDeferralReason::NotEvaluated};
  SmartRegionMsaaTraversalDiagnostic uia_async_msaa_diagnostic;
  SmartRegionCandidate uia_async_candidates[SmartRegionDiagnosticMaxCandidates];
  std::size_t uia_async_diagnostic_candidate_count{0};
  SmartRegionCandidateDiagnostic candidates[SmartRegionDiagnosticMaxCandidates];
  std::size_t candidate_count{0};
};

// 不分配内存、不持有窗口或图像的诊断开关与最近一次记录。
class SmartRegionDiagnosticTrace {
 public:
  void setEnabled(bool enabled) noexcept;
  bool enabled() const noexcept;
  bool record(const SmartRegionDiagnosticEvent& event) noexcept;
  bool recordOverlayRenderElapsed(std::uint64_t elapsed_ms,
                                  std::uint64_t render_count = 0) noexcept;
  bool recordOverlayRenderCount(std::uint64_t render_count) noexcept;
  bool recordHoverInputDelay(std::uint64_t delay_ms) noexcept;
  bool recordStabilizationDelay(std::uint64_t delay_ms) noexcept;
  bool recordHoverMotion(std::int64_t delta_x, std::int64_t delta_y,
                         std::uint64_t elapsed_ms, bool fast) noexcept;
  bool recordAsyncUiaResult(
      std::uint64_t request_id, std::uint64_t elapsed_ms,
      std::uint64_t age_ms, std::uint64_t poll_delay_ms, bool succeeded,
      bool msaa_attempted,
      bool browser_semantic_miss,
      bool cache_hit, bool suppressed_by_cooldown,
       std::size_t candidate_count, bool matches_current_request,
       bool applied, bool deferred,
       SmartRegionAsyncDeferralReason deferral_reason,
       const SmartRegionMsaaTraversalDiagnostic& msaa_diagnostic,
      const SmartRegionCandidate* candidates,
      std::size_t diagnostic_candidate_count) noexcept;
  bool hasLatestEvent() const noexcept;
  const SmartRegionDiagnosticEvent& latestEvent() const noexcept;

 private:
  bool m_enabled{false};
  bool m_has_latest_event{false};
  SmartRegionDiagnosticEvent m_latest_event;
};

const wchar_t* smartRegionDiagnosticSourceName(
    SmartRegionDiagnosticSource source) noexcept;
const wchar_t* smartRegionMsaaTraversalPathName(
    SmartRegionMsaaTraversalPath path) noexcept;
const wchar_t* smartRegionMsaaTraversalStopReasonName(
    SmartRegionMsaaTraversalStopReason stop_reason) noexcept;
const wchar_t* smartRegionMsaaFilteredNodeReasonName(
    SmartRegionMsaaFilteredNodeReason reason) noexcept;
const wchar_t* smartRegionCandidateRejectionName(
    SmartRegionCandidateRejection rejection) noexcept;
const wchar_t* smartRegionAsyncDeferralReasonName(
    SmartRegionAsyncDeferralReason reason) noexcept;

}  // namespace qingying
