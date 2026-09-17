#pragma once

#include <cstddef>
#include <cstdint>

#include "qingying/window/window_detector.hpp"

namespace qingying {

struct Image;
struct SmartRegionDiagnosticEvent;

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

// UIA 候选的质量等级。仅 UIA 候选填充，其他来源保持 None。
enum class SmartRegionUiaQuality : std::uint8_t {
  None,
  Disabled,
  UnnamedActionable,
  PatternActionable,
  NamedActionable,
  ContentSurface,
  GenericContainer,
};

struct SmartRegionUiaMetadata {
  bool available{false};
  bool is_control_element{false};
  bool is_content_element{false};
  bool is_enabled{false};
  bool is_keyboard_focusable{false};
  bool has_name{false};
  std::uint32_t control_type_id{0};
  std::uint8_t pattern_flags{0};
  SmartRegionUiaQuality quality{SmartRegionUiaQuality::None};
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
  // MSAA 使用 role，UIA 使用 ControlTypeId；0 表示来源未提供。
  std::uint32_t accessibility_role{0};
  // 命中对象相对于无障碍根的层级，仅用于诊断与候选质量判断。
  std::uint8_t accessibility_depth{0};
  SmartRegionUiaMetadata uia_metadata;

  bool valid() const noexcept;
  bool contains(int screen_x, int screen_y) const noexcept;
};

// 固定容量中始终为已知内容/视觉、客户区和窗口保留四个回退槽。
constexpr std::size_t SmartRegionMaxCandidates = 9;
constexpr std::size_t SmartRegionReservedFallbackCandidates = 4;
// UIA 保留命中元素和三个父级，剩余一个无障碍槽只供 MSAA 兜底使用。
constexpr std::size_t SmartRegionMaxAccessibilityCandidates =
    SmartRegionMaxCandidates - SmartRegionReservedFallbackCandidates;
constexpr std::size_t SmartRegionMaxUiaCandidates =
    SmartRegionMaxAccessibilityCandidates - 1;
// 保存鼠标位置下的有效候选层级。固定容量避免悬停热路径动态分配。
class SmartRegionCandidateCollection
{
 public:
  void replace(const SmartRegionCandidate* candidates,
               std::size_t candidate_count, int screen_x, int screen_y,
               const WindowRect& owner_rect,
               const SmartRegionCandidate& selected,
               std::uint8_t minimum_visual_confidence = 70) noexcept;
  bool cycle(int direction) noexcept;
  void clear() noexcept;
  bool empty() const noexcept;
  std::size_t count() const noexcept;
  const SmartRegionCandidate& current() const noexcept;
  const SmartRegionCandidate& candidateAt(std::size_t index) const noexcept;

 private:
  SmartRegionCandidate m_candidates[SmartRegionMaxCandidates];
  std::size_t m_count{0};
  std::size_t m_current_index{0};
};

// 视觉兜底所需的冻结背景图与其屏幕坐标范围；不拥有图像数据。
struct SmartRegionVisualContext {
  const Image* background{nullptr};
  WindowRect image_screen_rect;

  bool valid() const noexcept;
};

// 快速检测路径已经取得的窗口信息。Overlay 将其直接传递给后台 UIA
// 请求，避免一次悬停更新重复执行 WindowDetector。
struct SmartRegionWindowSnapshot
{
  std::uintptr_t root_window{0};
  WindowRect owner_rect;
  WindowRect client_rect;
  bool is_chromium_browser_chrome{false};
  std::uint8_t minimum_visual_confidence{70};

  bool valid() const noexcept;
};

enum class SmartRegionDetectionPolicy : std::uint8_t
{
  Complete,
  FastFallbackOnly,
  WindowOnly,
  UiSnapshot,
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
  static bool selectBest(const SmartRegionCandidate* candidates,
                         std::size_t candidate_count, int screen_x,
                         int screen_y, const WindowRect& owner_rect,
                         SmartRegionCandidate& out,
                         SmartRegionDiagnosticEvent& diagnostics,
                         std::uint8_t minimum_visual_confidence) noexcept;
  static bool selectBest(const SmartRegionCandidate* candidates,
                         std::size_t candidate_count, int screen_x,
                         int screen_y, const WindowRect& owner_rect,
                         SmartRegionCandidate& out,
                         std::uint8_t minimum_visual_confidence) noexcept;
  static bool hasValidLocalCandidate(
      const SmartRegionCandidate* candidates, std::size_t candidate_count,
      int screen_x, int screen_y, const WindowRect& owner_rect,
      SmartRegionDiagnosticSource source) noexcept;
};

// 延迟切换相邻候选区域，防止指针经过重叠子窗口时高亮框闪烁。
class SmartRegionHoverStabilizer {
 public:
  static constexpr std::uint64_t CandidateSwitchDelayMs = 48;

  bool update(const SmartRegionCandidate& candidate,
              std::uint64_t now_ms) noexcept;
  bool update(const SmartRegionCandidate& candidate,
              std::uint64_t now_ms, POINT screen_point,
              bool preserve_browser_wide_visual_fallback = false) noexcept;
  void clear() noexcept;
  bool hasStableCandidate() const noexcept;
  bool hasPendingCandidate() const noexcept;
  std::uint64_t pendingSinceMs() const noexcept;
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
  // 从处理开始时刻计算下一次允许处理时间，避免将一次检测耗时再叠加到
  // 固定节流间隔上。
  void markStarted(std::uint64_t now_ms) noexcept;
  void markProcessed(std::uint64_t now_ms) noexcept;
  std::uint64_t remainingDelayMs(std::uint64_t now_ms) const noexcept;
  void reset() noexcept;

 private:
  bool m_has_last_update{false};
  std::uint64_t m_last_update_ms{0};
};

// 在原始鼠标消息到达时记录短暂的快速移动段。仅 Chromium 顶部控件区使用
// 该状态延后异步候选的绘制，避免高频跨进程结果反复触发覆盖层重绘。
class SmartRegionAsyncPresentationGate {
 public:
  static constexpr std::int64_t FastMotionMinimumDeltaPx = 4;
  static constexpr std::uint64_t FastMotionMaximumSampleIntervalMs = 32;
  static constexpr std::uint64_t FastMotionHoldMs = 24;

  void recordRawMotion(int screen_x, int screen_y,
                       std::uint64_t now_ms) noexcept;
  bool hasRecentFastMotion(std::uint64_t now_ms) const noexcept;
  bool shouldDeferAsyncResult(bool is_chromium_browser_chrome,
                              std::uint64_t now_ms) const noexcept;
  std::int64_t latestDeltaX() const noexcept;
  std::int64_t latestDeltaY() const noexcept;
  std::uint64_t latestElapsedMs() const noexcept;
  void reset() noexcept;

 private:
  int m_last_screen_x{0};
  int m_last_screen_y{0};
  std::int64_t m_latest_delta_x{0};
  std::int64_t m_latest_delta_y{0};
  std::uint64_t m_latest_elapsed_ms{0};
  std::uint64_t m_last_raw_motion_at_ms{0};
  std::uint64_t m_last_fast_motion_at_ms{0};
  bool m_has_raw_motion_sample{false};
  bool m_has_fast_motion_sample{false};
};

}  // namespace qingying
