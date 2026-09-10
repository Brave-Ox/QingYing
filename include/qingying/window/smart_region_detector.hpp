#pragma once

#include <cstddef>
#include <cstdint>

#include "qingying/action/image.hpp"
#include "qingying/window/window_detector.hpp"

namespace qingying {

// 标识由智能吸附选择的区域层级。
enum class SmartRegionKind : std::uint8_t {
  None,
  KnownContent,
  ClientArea,
  Window,
};

// 智能吸附诊断中候选的来源。后续可在不改变日志协议的前提下补充 UIA / 视觉兜底。
enum class SmartRegionDiagnosticSource : std::uint8_t {
  None,
  Uia,
  Msaa,
  KnownContent,
  Visual,
  ClientArea,
  Window,
};

// 候选区域表达的交互层级，用于避免把泛化容器误判成独立操作单元。
enum class SmartRegionSemantic : std::uint8_t {
  Unknown,
  ActionableControl,
  ContentSurface,
  Fallback,
};

// 智能吸附候选区域。rect 使用物理屏幕坐标，right/bottom 为开区间。
struct SmartRegionCandidate {
  std::uintptr_t owner_window{0};
  std::uintptr_t target_window{0};
  WindowRect rect;
  SmartRegionKind kind{SmartRegionKind::None};
  SmartRegionDiagnosticSource source{SmartRegionDiagnosticSource::None};
  SmartRegionSemantic semantic{SmartRegionSemantic::Unknown};
  std::uint8_t visual_confidence{0};

  bool valid() const noexcept;
  bool contains(int screen_x, int screen_y) const noexcept;
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

// UIA 最多保留命中元素和三个父级，另有 MSAA、已知内容、视觉、客户区和窗口兜底。
constexpr std::size_t SmartRegionMaxCandidates = 9;
constexpr std::size_t SmartRegionDiagnosticMaxCandidates =
    SmartRegionMaxCandidates;

// 单个候选的诊断快照；固定容量，避免在鼠标移动路径上额外分配内存。
struct SmartRegionCandidateDiagnostic {
  SmartRegionCandidate candidate;
  int score{0};
  int source_score{0};
  int semantic_score{0};
  int pointer_score{0};
  int area_score{0};
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
  std::uint64_t elapsed_ms{0};
  std::uint64_t window_detection_ms{0};
  std::uint64_t uia_lookup_ms{0};
  std::uint64_t msaa_lookup_ms{0};
  std::uint64_t known_content_lookup_ms{0};
  std::uint64_t visual_lookup_ms{0};
  std::uint64_t selection_ms{0};
  std::uint64_t overlay_render_ms{0};
  std::uint64_t stabilization_delay_ms{0};
  std::uint8_t visual_edge_mask{0};
  bool window_detection_attempted{false};
  bool uia_lookup_attempted{false};
  bool uia_has_valid_local_candidate{false};
  bool msaa_lookup_attempted{false};
  bool msaa_candidate_found{false};
  bool known_content_lookup_attempted{false};
  bool visual_lookup_attempted{false};
  SmartRegionCandidateDiagnostic candidates[SmartRegionDiagnosticMaxCandidates];
  std::size_t candidate_count{0};
};

// 视觉兜底所需的冻结背景图与其屏幕坐标范围；不拥有图像数据。
struct SmartRegionVisualContext {
  const Image* background{nullptr};
  WindowRect image_screen_rect;

  bool valid() const noexcept;
};

enum class SmartRegionDetectionPolicy : std::uint8_t
{
  Complete,
  FastFallbackOnly,
};

// 从同一点命中的多个候选中选择最小且可独立操作的有效区域。
class SmartRegionCandidateSelector {
 public:
  static bool selectBest(const SmartRegionCandidate* candidates,
                         std::size_t candidate_count, int screen_x,
                         int screen_y, const WindowRect& owner_rect,
                         SmartRegionCandidate& out) noexcept;
  static bool selectBest(const SmartRegionCandidate* candidates,
                         std::size_t candidate_count, int screen_x,
                         int screen_y, const WindowRect& owner_rect,
                         SmartRegionCandidate& out,
                         SmartRegionDiagnosticEvent& diagnostics) noexcept;
  static bool hasValidLocalCandidate(
      const SmartRegionCandidate* candidates, std::size_t candidate_count,
      int screen_x, int screen_y, const WindowRect& owner_rect,
      SmartRegionDiagnosticSource source) noexcept;
};

// 不分配内存、不持有窗口或图像的诊断开关与最近一次记录。
class SmartRegionDiagnosticTrace {
 public:
  void setEnabled(bool enabled) noexcept;
  bool enabled() const noexcept;
  bool record(const SmartRegionDiagnosticEvent& event) noexcept;
  bool recordOverlayRenderElapsed(std::uint64_t elapsed_ms) noexcept;
  bool recordStabilizationDelay(std::uint64_t delay_ms) noexcept;
  bool hasLatestEvent() const noexcept;
  const SmartRegionDiagnosticEvent& latestEvent() const noexcept;

 private:
  bool m_enabled{false};
  bool m_has_latest_event{false};
  SmartRegionDiagnosticEvent m_latest_event;
};

const wchar_t* smartRegionDiagnosticSourceName(
    SmartRegionDiagnosticSource source) noexcept;
const wchar_t* smartRegionCandidateRejectionName(
    SmartRegionCandidateRejection rejection) noexcept;

// 延迟切换相邻候选区域，防止指针经过重叠子窗口时高亮框闪烁。
class SmartRegionHoverStabilizer {
 public:
  static constexpr std::uint64_t CandidateSwitchDelayMs = 48;

  bool update(const SmartRegionCandidate& candidate,
              std::uint64_t now_ms) noexcept;
  void clear() noexcept;
  bool hasStableCandidate() const noexcept;
  bool hasPendingCandidate() const noexcept;
  const SmartRegionCandidate& stableCandidate() const noexcept;
  // 点击确认时优先使用已经检测完成的最新局部候选；泛化回退候选不会覆盖
  // 当前稳定的局部候选。
  const SmartRegionCandidate& selectionCandidate() const noexcept;

 private:
  SmartRegionCandidate m_stable;
  SmartRegionCandidate m_pending;
  std::uint64_t m_pending_since_ms{0};
};

// 记录已绘制的悬停区域，仅在用户可见的高亮状态变化时请求覆盖层重绘。
class SmartRegionHoverRenderGate {
 public:
  bool update(const SmartRegionCandidate& candidate, bool has_hover) noexcept;

 private:
  bool m_has_rendered_state{false};
  bool m_has_hover{false};
  SmartRegionCandidate m_candidate;
};

// 将高频鼠标移动合并为受控的区域检测请求：首帧立即执行，后续只在最小间隔后执行。
// 该类型不依赖窗口、UIA 或图像，可在覆盖层消息循环中复用。
class SmartRegionUpdateGate {
 public:
  static constexpr std::uint64_t MinimumIntervalMs = 16;

  bool shouldProcess(std::uint64_t now_ms) const noexcept;
  void markProcessed(std::uint64_t now_ms) noexcept;
  std::uint64_t remainingDelayMs(std::uint64_t now_ms) const noexcept;
  void reset() noexcept;

 private:
  bool m_has_last_update{false};
  std::uint64_t m_last_update_ms{0};
};

// 根据屏幕坐标选择已知内容区、应用客户区或窗口边框。
class SmartRegionDetector {
 public:
  bool detectAt(int screen_x, int screen_y,
                SmartRegionCandidate& out,
                SmartRegionDiagnosticTrace* diagnostics = nullptr,
                const SmartRegionVisualContext* visual_context = nullptr,
                SmartRegionDetectionPolicy policy =
                    SmartRegionDetectionPolicy::Complete)
      const noexcept;
};

}  // namespace qingying
