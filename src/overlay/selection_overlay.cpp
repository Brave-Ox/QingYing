#include "qingying/overlay/selection_overlay.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <strsafe.h>
#include <utility>

#include "qingying/app/app_messages.hpp"
#include "qingying/app/ui_message_channel.h"
#include "qingying/overlay/coordinate_transform.hpp"
#include "qingying/overlay/overlay_phase.hpp"
#include "qingying/overlay/overlay_renderer.hpp"
#include "qingying/overlay/selection_controller.hpp"
#include "qingying/overlay/selection_handles.hpp"
#include "qingying/overlay/selection_toolbar.hpp"
#include "qingying/window/smart_region_detector.hpp"
#include "qingying/window/smart_region_mode.hpp"
#include "../window/uia_region_query_worker.hpp"

namespace qingying {

namespace {

// 截图期间临时注册热键：遮罩不抢前台激活权（WS_EX_NOACTIVATE），
// 键盘消息不会发给遮罩，因此通过 WM_HOTKEY 接收截图操作。
constexpr int kEscapeHotkeyId = 2;
constexpr int kCandidateNextHotkeyId = 7;
constexpr int kCandidatePreviousHotkeyId = 8;
constexpr UINT_PTR kHoverStabilizeTimerId = 3;
constexpr UINT_PTR kHoverUpdateTimerId = 4;
constexpr UINT_PTR kUiaResultPollTimerId = 5;
constexpr UINT kMinimumTimerDelayMs = 1;
constexpr UINT kUiaResultPollIntervalMs = 8;

// 覆盖层内部的拖拽类型：创建 / 调整大小 / 整体移动。
enum class DragKind { None, Create, Resize, Move };

// SelectionController 等 UI 状态通过窗口属性（WindowLongPtr）附加到窗口，
// 供 WndProc 在处理消息时访问，避免全局/静态可变量。
struct OverlayWindowData {
  SelectionController controller;
  SelectionCallback callback;
  SelectionClosedCallback closed_callback;
  std::atomic<std::uintptr_t>* owner_hwnd{nullptr};
  std::atomic<bool>* accepting_messages{nullptr};
  UiMessageChannel* message_channel{nullptr};
  bool window_destroyed{false};
  HWND overlay{nullptr};
  SelectionToolbar toolbar;
  SelectionAction action{SelectionAction::None};
  OverlayPhase phase{OverlayPhase::Sniffing};
  DragKind drag{DragKind::None};
  coord::VirtualScreenRect screen;
  // 按 DPI 缩放后的 UI 尺寸（100% 时等于基准常量）。
  int dpi{96};
  int handle_radius{handles::kHandleHitRadius};
  SelectionHandle active_handle{SelectionHandle::None};
  SmartRegionDetector smart_region_detector;
  SmartRegionVisualResultCache smart_region_visual_cache;
  SmartRegionDiagnosticTrace smart_region_diagnostics;
  SmartRegionHoverStabilizer hover_stabilizer;
  SmartRegionHoverRenderGate hover_render_gate;
  SmartRegionUpdateGate hover_update_gate;
  SmartRegionAsyncPresentationGate async_presentation_gate;
  window_detail::UiaRegionQueryWorker* uia_query_worker{nullptr};
  SmartRegionCandidate hover_candidate;
  SmartRegionCandidate fast_hover_candidate;
  SmartRegionCandidateCollection hover_candidates;
  SmartRegionModeSettings smart_region_mode_settings;
  SmartRegionMode smart_region_mode{SmartRegionMode::DetectElements};
  bool use_browser_chrome_visual_fallback{false};
  POINT hover_screen_point{};
  window_detail::UiaRegionQueryResult deferred_uia_result;
  std::int64_t hover_motion_delta_x{0};
  std::int64_t hover_motion_delta_y{0};
  std::uint64_t hover_motion_elapsed_ms{0};
  std::uint64_t smart_region_overlay_render_count{0};
  bool hover_motion_fast{false};
  bool has_deferred_uia_result{false};
  std::uint64_t uia_request_id{0};
  window_detail::UiaRegionQueryRequest current_uia_request;
  bool has_hover{false};
  bool has_pending_hover_update{false};
  bool first_frame_committed{false};
  int pending_hover_x{0};
  int pending_hover_y{0};
  std::uint64_t pending_hover_queued_at_ms{0};
  OverlayClientRect hover_rect;
  Image background;                // 遮罩界面背景（桌面截图，物理像素）；空则纯遮罩
  LongShotControlCallback longshot_control_callback;
  SelectionAction longshot_pending_action{SelectionAction::None};
  bool capture_passthrough{false};
  bool selection_locked{false};
  Image longshot_preview;
};

SelectionIntent toScreenSelection(const OverlayClientRect& client,
                                  const coord::VirtualScreenRect& screen);
bool updateOverlay(HWND hwnd, OverlayWindowData* data);
bool showToolbar(HWND overlay, OverlayWindowData* data,
                 const SelectionIntent& selection_screen,
                 const coord::VirtualScreenRect& screen);
void handleToolbarCommand(OverlayWindowData* data,
                          SelectionToolbarCommand command);
void requestHoverUpdate(HWND hwnd, OverlayWindowData* data, int client_x,
                        int client_y,
                        std::uint64_t queued_at_ms = 0);

const wchar_t kOverlayClassName[] = L"QingYingSelectionOverlay";

bool smartRegionDiagnosticsRequested() noexcept
{
  wchar_t value[2]{};
  const DWORD length = GetEnvironmentVariableW(
      L"QINGYING_SMART_REGION_DIAGNOSTICS", value,
      static_cast<DWORD>(std::size(value)));
  return length == 1 && value[0] == L'1';
}

void emitSmartRegionDiagnostic(const SmartRegionDiagnosticTrace& diagnostics)
{
  if (!diagnostics.enabled() || !diagnostics.hasLatestEvent()) {
    return;
  }
  const SmartRegionDiagnosticEvent& event = diagnostics.latestEvent();
  const wchar_t* const visual_cache_state =
      event.visual_cache_hit
          ? (event.visual_cache_contains_candidate ? L"positive" : L"negative")
          : L"miss";
  wchar_t message[4096]{};
  if (FAILED(StringCchPrintfW(
          message, std::size(message),
          L"[QingYing SmartRegion] source=%s rect=(%d,%d,%d,%d) root=%p "
          L"cursor=(%d,%d) pid=%lu process=%s class=%s total=%llu ms "
          L"window=%llu uia=%llu uiaLocal=%d msaaAttempted=%d msaa=%llu "
          L"msaaFound=%d known=%llu visual=%llu visualCache=%s "
           L"visualConfidence=%u "
           L"select=%llu render=%llu renderCount=%llu inputDelay=%llu "
           L"settle=%llu motion=(dx:%lld dy:%lld elapsed:%llu fast:%d) "
           L"edges=0x%02X asyncUia=(received:%d request:%llu "
           L"elapsed:%llu age:%llu pollDelay:%llu succeeded:%d msaa:%d semanticMiss:%d cache:%d cooldown:%d "
           L"resultCandidates:%llu current:%d applied:%d deferred:%d deferEligibility:%s) candidates=%llu\n",
          smartRegionDiagnosticSourceName(event.source), event.rect.left,
          event.rect.top, event.rect.right, event.rect.bottom,
          reinterpret_cast<void*>(event.root_window), event.cursor_x,
          event.cursor_y, static_cast<unsigned long>(event.process_id),
          event.process_name[0] != L'\0' ? event.process_name : L"<unknown>",
          event.window_class[0] != L'\0' ? event.window_class : L"<unknown>",
          static_cast<unsigned long long>(event.elapsed_ms),
          static_cast<unsigned long long>(event.window_detection_ms),
          static_cast<unsigned long long>(event.uia_lookup_ms),
          event.uia_has_valid_local_candidate ? 1 : 0,
          event.msaa_lookup_attempted ? 1 : 0,
          static_cast<unsigned long long>(event.msaa_lookup_ms),
          event.msaa_candidate_found ? 1 : 0,
          static_cast<unsigned long long>(event.known_content_lookup_ms),
          static_cast<unsigned long long>(event.visual_lookup_ms),
          visual_cache_state,
           static_cast<unsigned int>(event.visual_candidate_confidence),
           static_cast<unsigned long long>(event.selection_ms),
           static_cast<unsigned long long>(event.overlay_render_ms),
           static_cast<unsigned long long>(event.overlay_render_count),
           static_cast<unsigned long long>(event.hover_input_delay_ms),
           static_cast<unsigned long long>(event.stabilization_delay_ms),
           static_cast<long long>(event.hover_motion_delta_x),
           static_cast<long long>(event.hover_motion_delta_y),
           static_cast<unsigned long long>(event.hover_motion_elapsed_ms),
           event.hover_motion_fast ? 1 : 0,
           static_cast<unsigned int>(event.visual_edge_mask),
          event.uia_async_result_received ? 1 : 0,
          static_cast<unsigned long long>(event.uia_async_request_id),
          static_cast<unsigned long long>(event.uia_async_elapsed_ms),
          static_cast<unsigned long long>(event.uia_async_age_ms),
          static_cast<unsigned long long>(event.uia_async_poll_delay_ms),
          event.uia_async_result_succeeded ? 1 : 0,
          event.uia_async_msaa_attempted ? 1 : 0,
          event.uia_async_browser_semantic_miss ? 1 : 0,
          event.uia_async_cache_hit ? 1 : 0,
          event.uia_async_suppressed_by_cooldown ? 1 : 0,
           static_cast<unsigned long long>(event.uia_async_candidate_count),
           event.uia_async_matches_current_request ? 1 : 0,
           event.uia_async_result_applied ? 1 : 0,
           event.uia_async_result_deferred ? 1 : 0,
           smartRegionAsyncDeferralReasonName(
               event.uia_async_deferral_reason),
           static_cast<unsigned long long>(event.candidate_count)))) {
    return;
  }
  for (std::size_t index = 0; index < event.candidate_count; ++index) {
    const SmartRegionCandidateDiagnostic& candidate = event.candidates[index];
    wchar_t candidate_message[384]{};
    if (FAILED(StringCchPrintfW(
            candidate_message, std::size(candidate_message),
            L"  candidate[%llu] source=%s semantic=%u rect=(%d,%d,%d,%d) "
            L"area=%lld coverage=%u confidence=%u score=%d selected=%d "
            L"parts=(source:%d semantic:%d pointer:%d area:%d quality:%d "
            L"boundary:%d hierarchy:%d) reason=%s\n",
            static_cast<unsigned long long>(index),
            smartRegionDiagnosticSourceName(candidate.candidate.source),
            static_cast<unsigned int>(candidate.candidate.semantic),
            candidate.candidate.rect.left, candidate.candidate.rect.top,
            candidate.candidate.rect.right, candidate.candidate.rect.bottom,
            static_cast<long long>(candidate.area),
            static_cast<unsigned int>(candidate.owner_coverage_percent),
            static_cast<unsigned int>(
                candidate.candidate.visual_confidence),
            candidate.score, candidate.selected ? 1 : 0,
            candidate.source_score, candidate.semantic_score,
            candidate.pointer_score, candidate.area_score,
            candidate.quality_score, candidate.boundary_score,
            candidate.hierarchy_score,
            smartRegionCandidateRejectionName(candidate.rejection))) ||
        FAILED(StringCchCatW(message, std::size(message), candidate_message))) {
      break;
    }
  }
  OutputDebugStringW(message);
  if (event.uia_async_msaa_attempted)
  {
    wchar_t msaa_message[384]{};
    if (SUCCEEDED(StringCchPrintfW(
            msaa_message, std::size(msaa_message),
            L"[QingYing SmartRegion] asyncMsaa request=%llu path=%s "
            L"stop=%s visited=%llu\n",
            static_cast<unsigned long long>(event.uia_async_request_id),
            smartRegionMsaaTraversalPathName(
                event.uia_async_msaa_diagnostic.path),
            smartRegionMsaaTraversalStopReasonName(
                event.uia_async_msaa_diagnostic.stop_reason),
            static_cast<unsigned long long>(
                event.uia_async_msaa_diagnostic.visited_child_count))))
    {
      OutputDebugStringW(msaa_message);
    }
    const SmartRegionMsaaFilteredNodeDiagnostic& filtered_node =
        event.uia_async_msaa_diagnostic.filtered_node;
    if (filtered_node.reason != SmartRegionMsaaFilteredNodeReason::None)
    {
      wchar_t filtered_message[384]{};
      if (SUCCEEDED(StringCchPrintfW(
              filtered_message, std::size(filtered_message),
              L"[QingYing SmartRegion] asyncMsaaFiltered request=%llu "
              L"reason=%s role=%ld state=0x%08lX depth=%u "
              L"rect=(%d,%d,%d,%d)\n",
              static_cast<unsigned long long>(event.uia_async_request_id),
              smartRegionMsaaFilteredNodeReasonName(filtered_node.reason),
              static_cast<long>(filtered_node.role),
              static_cast<unsigned long>(filtered_node.state),
              static_cast<unsigned int>(filtered_node.accessibility_depth),
              filtered_node.rect.left, filtered_node.rect.top,
              filtered_node.rect.right, filtered_node.rect.bottom)))
      {
        OutputDebugStringW(filtered_message);
      }
    }
  }
  for (std::size_t index = 0;
       index < event.uia_async_diagnostic_candidate_count; ++index)
  {
    const SmartRegionCandidate& candidate = event.uia_async_candidates[index];
    wchar_t candidate_message[384]{};
    if (SUCCEEDED(StringCchPrintfW(
            candidate_message, std::size(candidate_message),
            L"[QingYing SmartRegion] asyncCandidate request=%llu index=%llu "
            L"source=%s semantic=%u role=%u depth=%u rect=(%d,%d,%d,%d)\n",
            static_cast<unsigned long long>(event.uia_async_request_id),
            static_cast<unsigned long long>(index),
            smartRegionDiagnosticSourceName(candidate.source),
            static_cast<unsigned int>(candidate.semantic),
            static_cast<unsigned int>(candidate.accessibility_role),
            static_cast<unsigned int>(candidate.accessibility_depth),
            candidate.rect.left, candidate.rect.top, candidate.rect.right,
            candidate.rect.bottom)))
    {
      OutputDebugStringW(candidate_message);
    }
  }
}

void registerOverlayHotkey(HWND hwnd, int hotkey_id, UINT modifiers,
                           UINT virtual_key,
                           const wchar_t* description) noexcept
{
  if (RegisterHotKey(hwnd, hotkey_id, modifiers, virtual_key) != FALSE)
  {
    return;
  }

  wchar_t message[256]{};
  if (SUCCEEDED(StringCchPrintfW(
          message, std::size(message),
          L"[QingYing SmartRegion] hotkey registration failed: %s "
          L"error=%lu\n",
          description != nullptr ? description : L"<unknown>",
          static_cast<unsigned long>(GetLastError()))))
  {
    OutputDebugStringW(message);
  }
}

void clearHover(OverlayWindowData* data) noexcept
{
  if (data == nullptr) {
    return;
  }
  if (data->overlay != nullptr) {
    // 销毁窗口或结束交互时无需依赖计时器返回值；状态已由下面字段清空。
    static_cast<void>(KillTimer(data->overlay, kHoverStabilizeTimerId));
    static_cast<void>(KillTimer(data->overlay, kHoverUpdateTimerId));
    static_cast<void>(KillTimer(data->overlay, kUiaResultPollTimerId));
  }
  if (data->uia_query_worker != nullptr)
  {
    data->uia_query_worker->clear();
  }
  data->hover_stabilizer.clear();
  data->smart_region_visual_cache.clear();
  data->hover_update_gate.reset();
  data->async_presentation_gate.reset();
  data->hover_candidate = SmartRegionCandidate{};
  data->fast_hover_candidate = SmartRegionCandidate{};
  data->current_uia_request = window_detail::UiaRegionQueryRequest{};
  data->deferred_uia_result = window_detail::UiaRegionQueryResult{};
  data->pending_hover_queued_at_ms = 0;
  data->use_browser_chrome_visual_fallback = false;
  data->hover_candidates.clear();
  data->hover_rect = OverlayClientRect{};
  data->has_hover = false;
  data->has_pending_hover_update = false;
  data->has_deferred_uia_result = false;
}

void destroyToolbar(OverlayWindowData* data) {
  if (data == nullptr) {
    return;
  }
  data->toolbar.hide();
}

// The window data is deliberately kept by SelectionOverlay until the owner
// can safely start the next window.  Release image payloads at WM_NCDESTROY so
// that keeping the small lifecycle record alive does not keep a full desktop
// screenshot (or a long-shot result) alive while the application is idle.
void releaseImagePayload(OverlayWindowData* data) noexcept {
  if (data == nullptr) {
    return;
  }
  data->background = Image{};
  data->longshot_preview = Image{};
}

void refreshToolbar(OverlayWindowData* data) {
  if (data != nullptr) {
    data->toolbar.update(data->phase);
  }
}

void requestLongShotStop(OverlayWindowData* data) {
  if (data == nullptr || !overlayPhaseIsLongShot(data->phase) ||
      data->phase == OverlayPhase::LongShotFinishing) {
    return;
  }
  if (!transitionOverlayPhase(data->phase,
                              OverlayPhase::LongShotFinishing)) {
    return;
  }
  refreshToolbar(data);
  if (data->longshot_control_callback) {
    data->longshot_control_callback(LongShotControl::Stop);
  }
}

void chooseToolbarAction(OverlayWindowData* data, SelectionAction action) {
  if (data == nullptr || data->overlay == nullptr) {
    return;
  }
  if (action == SelectionAction::LongShot) {
    if (data->phase == OverlayPhase::LongShotRunning ||
        data->phase == OverlayPhase::LongShotPaused) {
      const OverlayPhase next =
          data->phase == OverlayPhase::LongShotRunning
              ? OverlayPhase::LongShotPaused
              : OverlayPhase::LongShotRunning;
      if (!transitionOverlayPhase(data->phase, next)) {
        return;
      }
      refreshToolbar(data);
      if (data->longshot_control_callback) {
        data->longshot_control_callback(LongShotControl::TogglePause);
      }
      updateOverlay(data->overlay, data);
      return;
    }

    if (!transitionOverlayPhase(data->phase,
                                OverlayPhase::LongShotRunning)) {
      return;
    }
    data->action = SelectionAction::LongShot;
    data->longshot_pending_action = SelectionAction::None;
    clearHover(data);
    // During capture the selection hole must remain a real transparent hole;
    // otherwise the static desktop background would be captured repeatedly.
    data->capture_passthrough = true;
    refreshToolbar(data);
    updateOverlay(data->overlay, data);

    SelectionIntent result = toScreenSelection(data->controller.selection(),
                                               data->screen);
    result.action = SelectionAction::LongShot;
    if (data->callback) {
      data->callback(result);
    }
    return;
  }
  if (overlayPhaseIsLongShot(data->phase)) {
    if (data->phase == OverlayPhase::LongShotPaused) {
      data->longshot_pending_action = action;
      requestLongShotStop(data);
    }
    return;
  }
  data->action = action;
  PostMessageW(data->overlay, WM_CLOSE, 0, 0);
}

void handleToolbarCommand(OverlayWindowData* data,
                          SelectionToolbarCommand command) {
  if (data == nullptr) {
    return;
  }
  switch (command) {
    case SelectionToolbarCommand::Copy:
      chooseToolbarAction(data, SelectionAction::Copy);
      break;
    case SelectionToolbarCommand::Save:
      chooseToolbarAction(data, SelectionAction::Save);
      break;
    case SelectionToolbarCommand::ToggleLongShot:
      chooseToolbarAction(data, SelectionAction::LongShot);
      break;
    case SelectionToolbarCommand::Edit:
      chooseToolbarAction(data, SelectionAction::Edit);
      break;
    case SelectionToolbarCommand::Pin:
      chooseToolbarAction(data, SelectionAction::Pin);
      break;
    case SelectionToolbarCommand::StopLongShot:
      requestLongShotStop(data);
      break;
    case SelectionToolbarCommand::Cancel:
      data->controller.cancel();
      data->action = SelectionAction::None;
      clearHover(data);
      PostMessageW(data->overlay, WM_CLOSE, 0, 0);
      break;
  }
}

// Win32 系统光标资源：使用显式资源编号，避免项目未定义 UNICODE 时
// IDC_* 宏与 LoadCursorW 的字符类型不匹配。
HCURSOR loadOverlayCursor(SelectionHandle handle, bool has_selection) {
  int resource_id = 32512;  // IDC_ARROW
  if (!has_selection) {
    resource_id = 32515;  // IDC_CROSS
  } else {
    switch (handle) {
      case SelectionHandle::Top:
      case SelectionHandle::Bottom:
        resource_id = 32645;  // IDC_SIZENS
        break;
      case SelectionHandle::Left:
      case SelectionHandle::Right:
        resource_id = 32644;  // IDC_SIZEWE
        break;
      case SelectionHandle::TopLeft:
      case SelectionHandle::BottomRight:
        resource_id = 32642;  // IDC_SIZENWSE
        break;
      case SelectionHandle::TopRight:
      case SelectionHandle::BottomLeft:
        resource_id = 32643;  // IDC_SIZENESW
        break;
      case SelectionHandle::Move:
        resource_id = 32646;  // IDC_SIZEALL
        break;
      case SelectionHandle::None:
        break;
    }
  }
  return LoadCursorW(nullptr, MAKEINTRESOURCEW(resource_id));
}

void updateOverlayCursor(OverlayWindowData* data, int x, int y) {
  if (data == nullptr) {
    return;
  }

  const bool selection_editable =
      overlayPhaseHasSelection(data->phase) && !data->selection_locked;
  SelectionHandle handle = SelectionHandle::None;
  if (data->drag == DragKind::Resize || data->drag == DragKind::Move) {
    handle = data->active_handle;
  } else if (selection_editable) {
    handle = data->controller.hitTest(x, y);
  }

  HCURSOR cursor = loadOverlayCursor(
      handle, overlayPhaseHasSelection(data->phase));
  if (cursor != nullptr) {
    SetCursor(cursor);
  }
}

// 选区从覆盖层客户区坐标 → 屏幕坐标（供工具栏定位与最终出参使用）。
SelectionIntent toScreenSelection(const OverlayClientRect& client,
                                  const coord::VirtualScreenRect& screen) {
  SelectionIntent out;
  out.cancelled = client.empty();
  out.x = client.x + screen.x;
  out.y = client.y + screen.y;
  out.width = client.width;
  out.height = client.height;
  return out;
}

OverlayClientRect screenRectToOverlayClient(
    const WindowRect& screen_rect, const coord::VirtualScreenRect& screen)
{
  return {coord::screenToClientX(screen_rect.left, screen),
          coord::screenToClientY(screen_rect.top, screen),
          screen_rect.width(), screen_rect.height()};
}

void applyHoverCandidate(OverlayWindowData* data,
                         const SmartRegionCandidate& candidate,
                         POINT screen_point)
{
  if (data == nullptr || !candidate.valid())
  {
    return;
  }

  const std::uint64_t now_ms = GetTickCount64();
  const bool had_pending_candidate =
      data->hover_stabilizer.hasPendingCandidate();
  const std::uint64_t pending_since_ms =
      data->hover_stabilizer.pendingSinceMs();
  const bool stable_candidate_changed =
      data->hover_stabilizer.update(
          candidate, now_ms, screen_point,
          data->use_browser_chrome_visual_fallback);
  static_cast<void>(data->smart_region_diagnostics.recordStabilizationDelay(
      stable_candidate_changed && had_pending_candidate &&
              pending_since_ms != 0 && now_ms >= pending_since_ms
          ? now_ms - pending_since_ms
          : 0));
  if (!data->hover_stabilizer.hasStableCandidate()) {
    clearHover(data);
    return;
  }
  data->hover_candidate = data->hover_stabilizer.stableCandidate();
  data->hover_rect =
      screenRectToOverlayClient(data->hover_candidate.rect, data->screen);
  data->has_hover = !data->hover_rect.empty();
  if (data->hover_stabilizer.hasPendingCandidate()) {
    const UINT_PTR timer_id = SetTimer(
        data->overlay, kHoverStabilizeTimerId,
        static_cast<UINT>(SmartRegionHoverStabilizer::CandidateSwitchDelayMs),
        nullptr);
    if (timer_id == 0) {
      // 定时器创建失败时保留当前稳定候选；下一次鼠标移动会再次尝试切换。
      return;
    }
    return;
  }
  // 候选已稳定，不保留后台定时器，避免空闲覆盖层周期性重绘。
  static_cast<void>(KillTimer(data->overlay, kHoverStabilizeTimerId));
}

void recordRawHoverMotion(OverlayWindowData* data, int client_x,
                          int client_y) noexcept
{
  if (data == nullptr)
  {
    return;
  }
  const int screen_x = coord::clientToScreenX(client_x, data->screen);
  const int screen_y = coord::clientToScreenY(client_y, data->screen);
  const std::uint64_t now_ms = GetTickCount64();
  data->async_presentation_gate.recordRawMotion(screen_x, screen_y, now_ms);
  data->hover_motion_delta_x =
      data->async_presentation_gate.latestDeltaX();
  data->hover_motion_delta_y =
      data->async_presentation_gate.latestDeltaY();
  data->hover_motion_elapsed_ms =
      data->async_presentation_gate.latestElapsedMs();
  data->hover_motion_fast =
      data->async_presentation_gate.hasRecentFastMotion(now_ms);
}

bool cycleHoverCandidate(HWND hwnd, OverlayWindowData* data,
                         int direction) noexcept
{
  if (data == nullptr || data->phase != OverlayPhase::Sniffing ||
      data->drag != DragKind::None ||
      data->smart_region_mode != SmartRegionMode::DetectElements ||
      !data->hover_candidates.cycle(direction))
  {
    return false;
  }

  static_cast<void>(KillTimer(hwnd, kHoverStabilizeTimerId));
  data->hover_stabilizer.clear();
  data->smart_region_visual_cache.clear();
  applyHoverCandidate(data, data->hover_candidates.current(),
                      data->hover_screen_point);
  if (data->hover_render_gate.update(data->hover_candidate, data->has_hover))
  {
    static_cast<void>(updateOverlay(hwnd, data));
  }
  return true;
}

// UI 线程只执行本地窗口、已知区域和冻结截图视觉检测。跨进程 UIA/MSAA
// 由专用工作线程补充，避免无响应提供方阻塞 Overlay 消息循环。
void updateHover(OverlayWindowData* data, int client_x, int client_y)
{
  if (data == nullptr ||
      data->smart_region_mode == SmartRegionMode::Disabled)
  {
    return;
  }
  const int screen_x = coord::clientToScreenX(client_x, data->screen);
  const int screen_y = coord::clientToScreenY(client_y, data->screen);
  data->hover_screen_point = {screen_x, screen_y};
  const std::uint64_t motion_now_ms = GetTickCount64();
  data->hover_motion_delta_x =
      data->async_presentation_gate.latestDeltaX();
  data->hover_motion_delta_y =
      data->async_presentation_gate.latestDeltaY();
  data->hover_motion_elapsed_ms =
      data->async_presentation_gate.latestElapsedMs();
  data->hover_motion_fast =
      data->async_presentation_gate.hasRecentFastMotion(motion_now_ms);
  const WindowRect image_screen_rect{data->screen.x, data->screen.y,
                                     data->screen.right(),
                                     data->screen.bottom()};
  const SmartRegionVisualContext visual_context{&data->background,
                                                image_screen_rect};

  SmartRegionCandidate candidate;
  SmartRegionWindowSnapshot window_snapshot;
  const SmartRegionDetectionPolicy policy =
      data->smart_region_mode == SmartRegionMode::WindowOnly
          ? SmartRegionDetectionPolicy::WindowOnly
          : SmartRegionDetectionPolicy::FastFallbackOnly;
  if (!data->smart_region_detector.detectAt(
          screen_x, screen_y, candidate, &data->smart_region_diagnostics,
          &visual_context, policy, &data->hover_candidates,
          &window_snapshot, &data->smart_region_visual_cache))
  {
    static_cast<void>(data->smart_region_diagnostics.recordHoverMotion(
        data->hover_motion_delta_x, data->hover_motion_delta_y,
        data->hover_motion_elapsed_ms, data->hover_motion_fast));
    static_cast<void>(data->smart_region_diagnostics.recordOverlayRenderCount(
        data->smart_region_overlay_render_count));
    clearHover(data);
    return;
  }
  static_cast<void>(data->smart_region_diagnostics.recordHoverMotion(
      data->hover_motion_delta_x, data->hover_motion_delta_y,
      data->hover_motion_elapsed_ms, data->hover_motion_fast));
  static_cast<void>(data->smart_region_diagnostics.recordOverlayRenderCount(
      data->smart_region_overlay_render_count));
  data->fast_hover_candidate = candidate;
  data->use_browser_chrome_visual_fallback =
      window_snapshot.is_chromium_browser_chrome;
  applyHoverCandidate(data, candidate, data->hover_screen_point);

  if (data->smart_region_mode != SmartRegionMode::DetectElements)
  {
    return;
  }

  if (data->uia_query_worker == nullptr || !window_snapshot.valid())
  {
    return;
  }
  ++data->uia_request_id;
  data->current_uia_request = {
      data->uia_request_id,
      reinterpret_cast<HWND>(window_snapshot.root_window),
      {screen_x, screen_y},
      window_snapshot.owner_rect,
      GetTickCount64(),
      data->smart_region_diagnostics.enabled()};
  if (data->uia_query_worker->request(data->current_uia_request))
  {
    static_cast<void>(SetTimer(data->overlay, kUiaResultPollTimerId,
                               kUiaResultPollIntervalMs, nullptr));
  }
}

bool applyUiaQueryCandidate(
    HWND hwnd, OverlayWindowData* data,
    const window_detail::UiaRegionQueryResult& result, POINT screen_point,
    const WindowRect& owner_rect)
{
  if (data == nullptr)
  {
    return false;
  }

  SmartRegionCandidate candidate;
  if (!window_detail::selectUiaQueryCandidate(
          result, data->fast_hover_candidate, screen_point, owner_rect,
          candidate))
  {
    return false;
  }

  SmartRegionCandidate combined[SmartRegionMaxCandidates];
  std::size_t combined_count = window_detail::retainAccessibilityCandidates(
      result.candidates, result.candidate_count, combined, std::size(combined));
  for (std::size_t index = 0;
       index < data->hover_candidates.count() &&
       combined_count < SmartRegionMaxCandidates;
       ++index)
  {
    combined[combined_count++] = data->hover_candidates.candidateAt(index);
  }
  data->hover_candidates.replace(combined, combined_count, screen_point.x,
                                 screen_point.y, owner_rect, candidate);
  applyHoverCandidate(data, candidate, screen_point);
  if (data->hover_render_gate.update(data->hover_candidate, data->has_hover))
  {
    static_cast<void>(updateOverlay(hwnd, data));
  }
  return true;
}

bool applyDeferredUiaQueryResult(HWND hwnd, OverlayWindowData* data,
                                 POINT screen_point, bool force)
{
  if (data == nullptr || !data->has_deferred_uia_result ||
      (!force && data->async_presentation_gate.shouldDeferAsyncResult(
                     data->use_browser_chrome_visual_fallback,
                     GetTickCount64())))
  {
    return false;
  }

  const window_detail::UiaRegionQueryResult deferred_result =
      data->deferred_uia_result;
  data->deferred_uia_result = window_detail::UiaRegionQueryResult{};
  data->has_deferred_uia_result = false;
  window_detail::UiaRegionQueryRequest current_request =
      data->current_uia_request;
  current_request.screen_point = screen_point;
  if (!window_detail::isUiaQueryResultApplicable(
          deferred_result, current_request, GetTickCount64()))
  {
    return false;
  }
  return applyUiaQueryCandidate(hwnd, data, deferred_result, screen_point,
                                current_request.owner_rect);
}

void processUiaQueryResult(HWND hwnd, OverlayWindowData* data)
{
  if (data == nullptr || data->uia_query_worker == nullptr ||
      data->smart_region_mode != SmartRegionMode::DetectElements ||
      data->phase != OverlayPhase::Sniffing || data->drag != DragKind::None)
  {
    return;
  }

  const HWND current_root = data->current_uia_request.root_window;
  if (current_root != nullptr && IsWindow(current_root) == FALSE)
  {
    clearHover(data);
    static_cast<void>(updateOverlay(hwnd, data));
    return;
  }

  const std::uint64_t now_ms = GetTickCount64();
  if (data->has_deferred_uia_result &&
      !data->async_presentation_gate.shouldDeferAsyncResult(
          data->use_browser_chrome_visual_fallback, now_ms))
  {
    POINT screen_point{};
    if (GetCursorPos(&screen_point) != FALSE)
    {
      static_cast<void>(
          applyDeferredUiaQueryResult(hwnd, data, screen_point, false));
    }
  }

  window_detail::UiaRegionQueryResult result;
  if (data->uia_query_worker->tryTakeLatest(result))
  {
    const bool applies_to_current_request =
        window_detail::isUiaQueryResultApplicable(
            result, data->current_uia_request, now_ms);
    const std::uint64_t result_age_ms =
        now_ms >= result.requested_at_ms
            ? now_ms - result.requested_at_ms
            : 0;
    const std::uint64_t poll_delay_ms =
        result.completed_at_ms != 0 && now_ms >= result.completed_at_ms
            ? now_ms - result.completed_at_ms
            : 0;
    bool result_applied = false;
    bool result_deferred = false;
    const bool should_defer_async_result =
        data->async_presentation_gate.shouldDeferAsyncResult(
            data->use_browser_chrome_visual_fallback, now_ms);
    const SmartRegionAsyncDeferralReason deferral_reason =
        !data->use_browser_chrome_visual_fallback
            ? SmartRegionAsyncDeferralReason::NotChromiumBrowser
            : (should_defer_async_result
                   ? SmartRegionAsyncDeferralReason::FastMotion
                   : SmartRegionAsyncDeferralReason::MotionBelowThreshold);
    if (applies_to_current_request)
    {
      SmartRegionCandidate candidate;
      if (window_detail::selectUiaQueryCandidate(
              result, data->fast_hover_candidate,
              data->current_uia_request.screen_point,
              data->current_uia_request.owner_rect, candidate))
      {
        if (should_defer_async_result)
        {
          data->deferred_uia_result = result;
          data->has_deferred_uia_result = true;
          result_deferred = true;
        }
        else
        {
          result_applied = applyUiaQueryCandidate(
              hwnd, data, result, data->current_uia_request.screen_point,
              data->current_uia_request.owner_rect);
        }
      }
    }
    static_cast<void>(data->smart_region_diagnostics.recordOverlayRenderCount(
        data->smart_region_overlay_render_count));
    const bool recorded_async_result =
        data->smart_region_diagnostics.recordAsyncUiaResult(
            result.request_id, result.elapsed_ms, result_age_ms, poll_delay_ms,
            result.succeeded, result.msaa_attempted,
            result.browser_semantic_miss, result.cache_hit,
            result.suppressed_by_cooldown, result.candidate_count,
            applies_to_current_request, result_applied, result_deferred,
            deferral_reason,
            result.msaa_diagnostic,
            result.candidates, result.candidate_count);
    if (recorded_async_result)
    {
      emitSmartRegionDiagnostic(data->smart_region_diagnostics);
    }
  }
  if (!data->uia_query_worker->hasPendingWork() &&
      !data->has_deferred_uia_result)
  {
    static_cast<void>(KillTimer(hwnd, kUiaResultPollTimerId));
  }
}

void setSmartRegionMode(HWND hwnd, OverlayWindowData* data,
                        SmartRegionMode mode)
{
  if (data == nullptr || data->smart_region_mode == mode)
  {
    return;
  }

  clearHover(data);
  data->smart_region_mode = mode;
  // A persistence failure must not block the selected mode in this session.
  static_cast<void>(data->smart_region_mode_settings.save(mode));
  if (data->smart_region_diagnostics.enabled())
  {
    wchar_t message[128]{};
    if (SUCCEEDED(StringCchPrintfW(
            message, std::size(message),
            L"[QingYing SmartRegion] mode=%s\n", smartRegionModeName(mode))))
    {
      OutputDebugStringW(message);
    }
  }
  static_cast<void>(updateOverlay(hwnd, data));
  if (mode == SmartRegionMode::Disabled ||
      data->phase != OverlayPhase::Sniffing)
  {
    return;
  }

  POINT point{};
  if (GetCursorPos(&point) && ScreenToClient(hwnd, &point))
  {
    requestHoverUpdate(hwnd, data, point.x, point.y);
  }
}
void processHoverUpdate(HWND hwnd, OverlayWindowData* data, int client_x,
                        int client_y, std::uint64_t queued_at_ms)
{
  if (data == nullptr)
  {
    return;
  }
  updateHover(data, client_x, client_y);
  const std::uint64_t now_ms = GetTickCount64();
  const std::uint64_t input_delay_ms =
      now_ms >= queued_at_ms ? now_ms - queued_at_ms : 0;
  static_cast<void>(
      data->smart_region_diagnostics.recordHoverInputDelay(input_delay_ms));
  if (data->hover_render_gate.update(data->hover_candidate, data->has_hover))
  {
    static_cast<void>(updateOverlay(hwnd, data));
  }
  emitSmartRegionDiagnostic(data->smart_region_diagnostics);
}

// 鼠标移动频率可能远高于 UIA 查询和整屏绘制的处理速度。保留最后一个坐标，
// 用短定时器合并中间事件，首帧仍立即展示智能吸附框。
void requestHoverUpdate(HWND hwnd, OverlayWindowData* data, int client_x,
                        int client_y, std::uint64_t queued_at_ms)
{
  if (data == nullptr)
  {
    return;
  }

  const std::uint64_t now_ms = GetTickCount64();
  const std::uint64_t effective_queued_at_ms =
      queued_at_ms == 0 ? now_ms : queued_at_ms;
  if (data->hover_update_gate.shouldProcess(now_ms))
  {
    static_cast<void>(KillTimer(hwnd, kHoverUpdateTimerId));
    data->has_pending_hover_update = false;
    data->hover_update_gate.markStarted(now_ms);
    processHoverUpdate(hwnd, data, client_x, client_y,
                       effective_queued_at_ms);
    return;
  }

  data->pending_hover_x = client_x;
  data->pending_hover_y = client_y;
  data->pending_hover_queued_at_ms = effective_queued_at_ms;
  data->has_pending_hover_update = true;
  const std::uint64_t delay_ms =
      data->hover_update_gate.remainingDelayMs(now_ms);
  const UINT timer_delay_ms = static_cast<UINT>(
      delay_ms == 0 ? kMinimumTimerDelayMs : delay_ms);
  static_cast<void>(SetTimer(hwnd, kHoverUpdateTimerId, timer_delay_ms,
                             nullptr));
}

bool showToolbar(HWND overlay, OverlayWindowData* data,
                 const SelectionIntent& selection_screen,
                 const coord::VirtualScreenRect& screen) {
  if (data == nullptr) {
    return false;
  }
  SelectionToolbarPlacement placement;
  placement.selection_x = selection_screen.x;
  placement.selection_y = selection_screen.y;
  placement.selection_width = selection_screen.width;
  placement.selection_height = selection_screen.height;
  placement.screen_left = screen.x;
  placement.screen_top = screen.y;
  placement.screen_right = screen.right();
  placement.screen_bottom = screen.bottom();
  return data->toolbar.show(
      overlay, placement, data->phase,
      [data](SelectionToolbarCommand command) {
        handleToolbarCommand(data, command);
      });
}

// Re-render the layered-window frame from the current interaction state.
bool updateOverlay(HWND hwnd, OverlayWindowData* data) {
  if (data == nullptr) {
    return false;
  }
  const OverlayClientRect& selection = data->controller.selection();
  const OverlayRenderState render_state(
      selection, data->hover_rect, data->background, data->longshot_preview,
      data->phase,
      overlayPhaseHasSelection(data->phase) && !selection.empty() &&
          !data->selection_locked,
      data->handle_radius, data->drag == DragKind::None && data->has_hover,
      data->capture_passthrough);
  const bool diagnostics_enabled = data->smart_region_diagnostics.enabled() &&
                                   data->smart_region_diagnostics.hasLatestEvent();
  const std::uint64_t render_begin_ms =
      diagnostics_enabled ? GetTickCount64() : 0;
  const bool rendered =
      OverlayRenderer::render(hwnd, data->screen, render_state);
  if (diagnostics_enabled) {
    ++data->smart_region_overlay_render_count;
    static_cast<void>(
        data->smart_region_diagnostics.recordOverlayRenderElapsed(
            GetTickCount64() - render_begin_ms,
            data->smart_region_overlay_render_count));
  }
  return rendered;
}

LRESULT CALLBACK overlayWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                LPARAM lparam) {
  OverlayWindowData* data = reinterpret_cast<OverlayWindowData*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (msg) {
    case WM_NCCREATE: {
      const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
      OverlayWindowData* create_data =
          static_cast<OverlayWindowData*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(create_data));
      if (create_data != nullptr) {
        create_data->overlay = hwnd;
      }
      return TRUE;
    }
    case WM_QINGYING_SELECTION_OVERLAY_READY: {
      if (data == nullptr) {
        return 0;
      }
      if (!updateOverlay(hwnd, data)) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
      }
      data->first_frame_committed = true;
      // 首帧已绘制后再读取持久化模式，避免注册表访问进入唤起热路径。
      data->smart_region_mode = data->smart_region_mode_settings.load();
      if (data->uia_query_worker != nullptr)
      {
        static_cast<void>(data->uia_query_worker->start());
      }
      if (data->phase == OverlayPhase::Selected) {
        const SelectionIntent selection_screen =
            toScreenSelection(data->controller.selection(), data->screen);
        if (!showToolbar(hwnd, data, selection_screen, data->screen)) {
          data->controller.cancel();
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
      }
      return 0;
    }
    case WM_QINGYING_SELECTION_OVERLAY_FIRST_FRAME_QUERY:
    {
      return data != nullptr && data->first_frame_committed ? 1 : 0;
    }
    case WM_QINGYING_SELECTION_LONGSHOT_PREVIEW: {
      if (data != nullptr && data->message_channel != nullptr) {
        const auto message =
            data->message_channel->take<SelectionOverlayLongShotPreviewMessage>(
                static_cast<UiMessageToken>(lparam));
        if (message.has_value()) {
          data->longshot_preview = std::move(message->image);
          updateOverlay(hwnd, data);
        }
      }
      return 0;
    }
    case WM_QINGYING_SELECTION_LONGSHOT_FINISHED: {
      std::optional<SelectionOverlayLongShotFinishedMessage> message;
      if (data != nullptr && data->message_channel != nullptr) {
        message = data->message_channel->take<
            SelectionOverlayLongShotFinishedMessage>(
            static_cast<UiMessageToken>(lparam));
      }
      if (data != nullptr) {
        const bool success = message.has_value() && message->success;
        const SelectionAction pending_action = data->longshot_pending_action;
        const bool run_pending_action =
            success && pending_action != SelectionAction::None;
        if (!transitionOverlayPhase(data->phase, OverlayPhase::Selected)) {
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
          return 0;
        }
        data->capture_passthrough = false;
        data->longshot_pending_action = SelectionAction::None;
        if (!success) {
          // Failure dialogs are shown by Application only after this topmost
          // fullscreen window has gone away.
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
          return 0;
        }
        if (run_pending_action) {
          data->action = pending_action;
          PostMessageW(hwnd, WM_CLOSE, 0, 0);
          return 0;
        }
        refreshToolbar(data);
        updateOverlay(hwnd, data);
      }
      return 0;
    }
    case WM_QINGYING_SELECTION_OVERLAY_ABORT: {
      if (data != nullptr) {
        data->callback = {};
        data->closed_callback = {};
        data->longshot_control_callback = {};
        data->controller.cancel();
        data->action = SelectionAction::None;
      }
      if (data != nullptr) {
        (void)transitionOverlayPhase(data->phase, OverlayPhase::Closing);
      }
      DestroyWindow(hwnd);
      return 0;
    }
    case WM_SETCURSOR: {
      if (data == nullptr) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
      }
      POINT cursor_point{};
      GetCursorPos(&cursor_point);
      ScreenToClient(hwnd, &cursor_point);
      updateOverlayCursor(data, cursor_point.x, cursor_point.y);
      return TRUE;
    }
    case WM_LBUTTONDOWN: {
      if (data == nullptr) {
        return 0;
      }
      if (overlayPhaseIsLongShot(data->phase)) {
        return 0;
      }
      // 标注后的栅格结果与当前物理选区是一组不可拆分的结果。恢复操作条
      // 时锁定选区，避免拖动 / 缩放后继续对尺寸不匹配的旧图执行动作。
      if (data->selection_locked) {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));

      if (!overlayPhaseHasSelection(data->phase)) {
        const POINT screen_point{
            coord::clientToScreenX(x, data->screen),
            coord::clientToScreenY(y, data->screen)};
        static_cast<void>(
            applyDeferredUiaQueryResult(hwnd, data, screen_point, true));
        // 尚无有效选区：开始拖出矩形。
        if (!transitionOverlayPhase(data->phase, OverlayPhase::Creating)) {
          return 0;
        }
        SetCapture(hwnd);
        data->drag = DragKind::Create;
        data->active_handle = SelectionHandle::None;
        data->controller.begin(x, y);
        return 0;
      }

      // 已有有效选区：清除悬停，命中测试决定「调整 / 移动 / 重新框选」。
      clearHover(data);
      const SelectionHandle handle = data->controller.hitTest(x, y);
      if (handle == SelectionHandle::None) {
        destroyToolbar(data);
        if (!transitionOverlayPhase(data->phase, OverlayPhase::Creating)) {
          return 0;
        }
        data->action = SelectionAction::None;
        SetCapture(hwnd);
        data->drag = DragKind::Create;
        data->active_handle = SelectionHandle::None;
        data->controller.begin(x, y);
      } else if (handle == SelectionHandle::Move) {
        destroyToolbar(data);
        SetCapture(hwnd);
        data->drag = DragKind::Move;
        data->active_handle = SelectionHandle::Move;
        data->controller.beginMove(x, y);
      } else {
        destroyToolbar(data);
        SetCapture(hwnd);
        data->drag = DragKind::Resize;
        data->active_handle = handle;
        data->controller.beginResize(handle, x, y);
      }
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (data == nullptr) {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      updateOverlayCursor(data, x, y);

      if (data->drag == DragKind::None) {
        // 空闲悬停：无按键且未确认选区 → 智能吸附高亮。
        if ((wparam & MK_LBUTTON) == 0 &&
            data->phase == OverlayPhase::Sniffing) {
          recordRawHoverMotion(data, x, y);
          requestHoverUpdate(hwnd, data, x, y);
        }
        return 0;
      }

      if ((wparam & MK_LBUTTON) == 0) {
        return 0;
      }
      // 同坐标的 WM_MOUSEMOVE 不代表用户已开始手动框选；保留稳定候选，
      // 使纯点击仍能接受智能吸附区域。只有实际位移才退出智能候选模式。
      const bool create_selection_moved =
          data->drag == DragKind::Create &&
          data->controller.hasMovedFromStart(x, y);
      if (data->drag != DragKind::Create || create_selection_moved) {
        clearHover(data);
      }
      switch (data->drag) {
        case DragKind::Create:
          data->controller.update(x, y);
          break;
        case DragKind::Resize:
          data->controller.updateResize(x, y);
          break;
        case DragKind::Move:
          data->controller.updateMove(x, y);
          break;
        default:
          break;
      }
      updateOverlay(hwnd, data);
      return 0;
    }
    case WM_MOUSEWHEEL:
    {
      const int direction = GET_WHEEL_DELTA_WPARAM(wparam) < 0 ? 1 : -1;
      if (cycleHoverCandidate(hwnd, data, direction))
      {
        return 0;
      }
      return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    case WM_TIMER: {
      if (data == nullptr) {
        return 0;
      }
      if (wparam == kUiaResultPollTimerId)
      {
        if (data->phase != OverlayPhase::Sniffing ||
            data->drag != DragKind::None)
        {
          static_cast<void>(KillTimer(hwnd, kUiaResultPollTimerId));
          if (data->uia_query_worker != nullptr)
          {
            data->uia_query_worker->clear();
          }
          return 0;
        }
        processUiaQueryResult(hwnd, data);
        return 0;
      }
      if (wparam == kHoverUpdateTimerId) {
        static_cast<void>(KillTimer(hwnd, kHoverUpdateTimerId));
        if (data->phase != OverlayPhase::Sniffing ||
            data->drag != DragKind::None ||
            !data->has_pending_hover_update) {
          data->has_pending_hover_update = false;
          return 0;
        }
        const int x = data->pending_hover_x;
        const int y = data->pending_hover_y;
        const std::uint64_t queued_at_ms = data->pending_hover_queued_at_ms;
        data->has_pending_hover_update = false;
        data->pending_hover_queued_at_ms = 0;
        requestHoverUpdate(hwnd, data, x, y, queued_at_ms);
        return 0;
      }
      if (wparam != kHoverStabilizeTimerId ||
          data->phase != OverlayPhase::Sniffing ||
          data->drag != DragKind::None) {
        return 0;
      }
      POINT screen_point{};
      if (!GetCursorPos(&screen_point)) {
        clearHover(data);
        updateOverlay(hwnd, data);
        return 0;
      }
      requestHoverUpdate(
          hwnd, data,
          coord::screenToClientX(screen_point.x, data->screen),
          coord::screenToClientY(screen_point.y, data->screen));
      return 0;
    }
    case WM_LBUTTONUP: {
      if (data == nullptr || data->drag == DragKind::None) {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      ReleaseCapture();
      switch (data->drag) {
        case DragKind::Create:
          data->controller.update(x, y);
          data->controller.confirm();
          if (data->controller.selection().empty() && data->has_hover) {
            // 纯点击未拖拽：优先使用已经检测完成的最新局部候选，避免
            // 迟滞中的旧候选或整窗回退成为最终选区。
            const SmartRegionCandidate& selection_candidate =
                data->hover_stabilizer.selectionCandidate();
            const OverlayClientRect selection_rect =
                screenRectToOverlayClient(selection_candidate.rect,
                                          data->screen);
            data->controller.setSelection(
                selection_rect.x, selection_rect.y, selection_rect.width,
                selection_rect.height);
          }
          break;
        case DragKind::Resize:
          data->controller.updateResize(x, y);
          data->controller.endDrag();
          break;
        case DragKind::Move:
          data->controller.updateMove(x, y);
          data->controller.endDrag();
          break;
        default:
          break;
      }
      clearHover(data);
      data->drag = DragKind::None;
      data->active_handle = SelectionHandle::None;

      const OverlayClientRect& selection = data->controller.selection();
      if (selection.empty()) {
        (void)transitionOverlayPhase(data->phase, OverlayPhase::Sniffing);
        destroyToolbar(data);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
      }

      if (!transitionOverlayPhase(data->phase, OverlayPhase::Selected)) {
        data->controller.cancel();
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
      }
      const SelectionIntent selection_screen =
          toScreenSelection(selection, data->screen);
      if (!showToolbar(hwnd, data, selection_screen, data->screen)) {
        data->controller.cancel();
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
      }
      updateOverlay(hwnd, data);
      return 0;
    }
    case WM_RBUTTONDOWN: {
      if (data != nullptr) {
        if (overlayPhaseIsLongShot(data->phase)) {
          requestLongShotStop(data);
          return 0;
        }
        data->controller.cancel();
        data->action = SelectionAction::None;
        clearHover(data);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      }
      return 0;
    }
    case WM_HOTKEY:
    {
      if (data == nullptr)
      {
        return 0;
      }
      if (wparam == kCandidateNextHotkeyId)
      {
        static_cast<void>(cycleHoverCandidate(hwnd, data, 1));
        return 0;
      }
      if (wparam == kCandidatePreviousHotkeyId)
      {
        static_cast<void>(cycleHoverCandidate(hwnd, data, -1));
        return 0;
      }
      SmartRegionMode mode = SmartRegionMode::DetectElements;
      if (smartRegionModeForHotkeyId(static_cast<int>(wparam), mode))
      {
        setSmartRegionMode(hwnd, data, mode);
        return 0;
      }
      SelectionToolbarCommand command = SelectionToolbarCommand::Cancel;
      if (selectionToolbarHotkeyCommand(data->phase, static_cast<int>(wparam),
                                        command))
      {
        handleToolbarCommand(data, command);
        return 0;
      }
      if (wparam == kEscapeHotkeyId)
      {
        if (overlayPhaseIsLongShot(data->phase))
        {
          requestLongShotStop(data);
          return 0;
        }
        data->controller.cancel();
        data->action = SelectionAction::None;
        clearHover(data);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      }
      return 0;
    }
    case WM_KEYDOWN:
    {
      if (data != nullptr && wparam == VK_ESCAPE)
      {
        if (overlayPhaseIsLongShot(data->phase))
        {
          requestLongShotStop(data);
          return 0;
        }
        data->controller.cancel();
        data->action = SelectionAction::None;
        clearHover(data);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
      }
      return 0;
    }
    case WM_CLOSE: {
      if (data != nullptr) {
        (void)transitionOverlayPhase(data->phase, OverlayPhase::Closing);
      }
      destroyToolbar(data);
      DestroyWindow(hwnd);
      return 0;
    }
    case WM_DESTROY: {
      if (data != nullptr) {
        if (data->phase != OverlayPhase::Closing) {
          (void)transitionOverlayPhase(data->phase, OverlayPhase::Closing);
        }
        destroyToolbar(data);
        SelectionIntent result = toScreenSelection(data->controller.selection(),
                                                   data->screen);
        result.action = data->action;
        SelectionCallback callback = std::move(data->callback);
        SelectionClosedCallback closed_callback =
            std::move(data->closed_callback);
        if (data->owner_hwnd != nullptr) {
          data->owner_hwnd->store(0);
        }
        if (data->accepting_messages != nullptr) {
          data->accepting_messages->store(false);
        }
        if (data->message_channel != nullptr) {
          data->message_channel->drain();
        }
        UnregisterHotKey(hwnd, kEscapeHotkeyId);
        UnregisterHotKey(hwnd, kCandidateNextHotkeyId);
        UnregisterHotKey(hwnd, kCandidatePreviousHotkeyId);
        UnregisterHotKey(hwnd, SmartRegionDetectElementsModeHotkeyId);
        UnregisterHotKey(hwnd, SmartRegionWindowOnlyModeHotkeyId);
        UnregisterHotKey(hwnd, SmartRegionDisabledModeHotkeyId);
        UnregisterHotKey(hwnd, SmartRegionDetectElementsAlternateHotkeyId);
        UnregisterHotKey(hwnd, SmartRegionWindowOnlyAlternateHotkeyId);
        UnregisterHotKey(hwnd, SmartRegionDisabledAlternateHotkeyId);
        UnregisterHotKey(hwnd, SelectionToolbarCopyHotkeyId);
        UnregisterHotKey(hwnd, SelectionToolbarLongShotHotkeyId);
        if (callback && result.action != SelectionAction::LongShot) {
          callback(result);
        }
        if (closed_callback) {
          closed_callback();
        }
      }
      return 0;
    }
    case WM_NCDESTROY: {
      if (data != nullptr) {
        data->window_destroyed = true;
        if (data->uia_query_worker != nullptr)
        {
          data->uia_query_worker->clear();
        }
        if (data->accepting_messages != nullptr) {
          data->accepting_messages->store(false);
        }
        if (data->message_channel != nullptr) {
          data->message_channel->drain();
        }
        releaseImagePayload(data);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      }
      return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

struct SelectionOverlay::Impl {
  window_detail::UiaRegionQueryWorker uia_query_worker;
  std::unique_ptr<OverlayWindowData> window_data;
  UiMessageChannel messages;
  std::atomic<bool> accepting_messages{false};
};

SelectionOverlay::SelectionOverlay() : impl_(std::make_unique<Impl>()) {}

SelectionOverlay::~SelectionOverlay() {
  shutdown();
}

bool SelectionOverlay::show(const Image& background,
                             SelectionCallback callback,
                             LongShotControlCallback
                                 longshot_control_callback,
                             const SelectionIntent& initial_selection,
                             SelectionClosedCallback closed_callback,
                             bool initial_selection_locked) {
  if (isVisible()) {
    return false;
  }

  if (impl_->window_data != nullptr) {
    if (!impl_->window_data->window_destroyed) {
      return false;
    }
    impl_->window_data.reset();
  }
  impl_->messages.drain();

  HINSTANCE instance = GetModuleHandleW(nullptr);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.lpfnWndProc = overlayWndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32515));  // IDC_CROSS
  wc.lpszClassName = kOverlayClassName;
  if (RegisterClassExW(&wc) == 0 &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  auto data = std::make_unique<OverlayWindowData>();
  data->uia_query_worker = &impl_->uia_query_worker;
  data->message_channel = &impl_->messages;
  data->accepting_messages = &impl_->accepting_messages;
  data->smart_region_diagnostics.setEnabled(smartRegionDiagnosticsRequested());
  data->background = background;  // 桌面截图背景（物理像素）；空则纯遮罩
  data->callback = std::move(callback);
  data->closed_callback = std::move(closed_callback);
  data->longshot_control_callback = std::move(longshot_control_callback);
  data->selection_locked = initial_selection_locked;
  data->owner_hwnd = &overlay_hwnd_;
  data->screen = coord::getVirtualScreen();
  data->controller.setBounds(data->screen.width, data->screen.height);

  if (initial_selection.valid()) {
    const OverlayClientRect initial_client =
        coord::screenToClient(initial_selection.screenRect(), data->screen);
    data->controller.setSelection(initial_client.x, initial_client.y,
                                  initial_client.width, initial_client.height);
    if (!data->controller.selection().empty()) {
      (void)transitionOverlayPhase(data->phase, OverlayPhase::Selected);
    }
  }

  // 按 DPI 缩放 UI（100% 为 96 DPI）：手柄命中半径。
  data->dpi = coord::getSystemDpi();
  const auto scale = [&](int value) { return MulDiv(value, data->dpi, 96); };
  data->handle_radius = scale(handles::kHandleHitRadius);
  data->controller.setHandleRadius(data->handle_radius);

  HWND hwnd = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
      kOverlayClassName, L"", WS_POPUP | WS_VISIBLE, data->screen.x,
      data->screen.y, data->screen.width, data->screen.height, nullptr,
      nullptr, instance, data.get());
  if (hwnd == nullptr) {
    impl_->messages.drain();
    return false;
  }
  impl_->window_data = std::move(data);
  overlay_hwnd_.store(reinterpret_cast<std::uintptr_t>(hwnd));
  impl_->accepting_messages.store(true);

  // 遮罩不抢前台激活权：WS_EX_NOACTIVATE 保证点击/显示都不会激活遮罩，
  // 原前台窗口保持激活，其从属浮层（owned popup）不会因失活而隐藏。
  // 键盘操作改由截图期间的临时热键提供，窗口销毁时统一注销。
  (void)RegisterHotKey(hwnd, kEscapeHotkeyId, 0, VK_ESCAPE);
  (void)RegisterHotKey(hwnd, kCandidateNextHotkeyId, MOD_NOREPEAT, VK_TAB);
  (void)RegisterHotKey(hwnd, kCandidatePreviousHotkeyId,
                       MOD_SHIFT | MOD_NOREPEAT, VK_TAB);
  registerOverlayHotkey(hwnd, SmartRegionDetectElementsModeHotkeyId,
                        MOD_CONTROL | MOD_NOREPEAT, '1', L"Ctrl+1");
  registerOverlayHotkey(hwnd, SmartRegionWindowOnlyModeHotkeyId,
                        MOD_CONTROL | MOD_NOREPEAT, '2', L"Ctrl+2");
  registerOverlayHotkey(hwnd, SmartRegionDisabledModeHotkeyId,
                        MOD_CONTROL | MOD_NOREPEAT, '3', L"Ctrl+3");
  registerOverlayHotkey(hwnd, SmartRegionDetectElementsAlternateHotkeyId,
                        MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, '1',
                        L"Ctrl+Shift+1");
  registerOverlayHotkey(hwnd, SmartRegionWindowOnlyAlternateHotkeyId,
                        MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, '2',
                        L"Ctrl+Shift+2");
  registerOverlayHotkey(hwnd, SmartRegionDisabledAlternateHotkeyId,
                        MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, '3',
                        L"Ctrl+Shift+3");
  (void)RegisterHotKey(hwnd, SelectionToolbarCopyHotkeyId,
                       MOD_CONTROL | MOD_NOREPEAT,
                       SelectionToolbarCopyShortcutVirtualKey);
  (void)RegisterHotKey(hwnd, SelectionToolbarLongShotHotkeyId, MOD_NOREPEAT,
                       SelectionToolbarLongShotShortcutVirtualKey);

  // 首帧渲染（UpdateLayeredWindow 需要窗口可见）。
  PostMessageW(hwnd, WM_QINGYING_SELECTION_OVERLAY_READY, 0, 0);
  return true;
}

bool SelectionOverlay::postLongShotPreview(const Image& image) {
  const HWND hwnd = reinterpret_cast<HWND>(overlay_hwnd_.load());
  if (hwnd == nullptr || !impl_->accepting_messages.load()) {
    return false;
  }
  try {
    SelectionOverlayLongShotPreviewMessage message;
    message.image = OverlayRenderer::makeLongShotPreviewImage(image);
    const auto token = impl_->messages.push(std::move(message));
    if (!token.has_value()) {
      return false;
    }
    if (!PostMessageW(hwnd, WM_QINGYING_SELECTION_LONGSHOT_PREVIEW, 0,
                      static_cast<LPARAM>(*token))) {
      impl_->messages.discard(*token);
      return false;
    }
  } catch (...) {
    return false;
  }
  return true;
}

bool SelectionOverlay::postLongShotFinished(bool success) {
  const HWND hwnd = reinterpret_cast<HWND>(overlay_hwnd_.load());
  if (hwnd == nullptr || !impl_->accepting_messages.load()) {
    return false;
  }
  SelectionOverlayLongShotFinishedMessage message;
  message.success = success;
  const auto token = impl_->messages.push(std::move(message));
  if (!token.has_value()) {
    return false;
  }
  if (!PostMessageW(hwnd, WM_QINGYING_SELECTION_LONGSHOT_FINISHED, 0,
                    static_cast<LPARAM>(*token))) {
    impl_->messages.discard(*token);
    return false;
  }
  return true;
}

void SelectionOverlay::hide() {
  impl_->accepting_messages.store(false);
  const HWND hwnd = reinterpret_cast<HWND>(overlay_hwnd_.load());
  if (hwnd != nullptr && IsWindow(hwnd)) {
    const DWORD window_thread = GetWindowThreadProcessId(hwnd, nullptr);
    if (window_thread == GetCurrentThreadId()) {
      SendMessageW(hwnd, WM_QINGYING_SELECTION_OVERLAY_ABORT, 0, 0);
    } else {
      PostMessageW(hwnd, WM_QINGYING_SELECTION_OVERLAY_ABORT, 0, 0);
    }
  }
  impl_->messages.drain();
}

void SelectionOverlay::beginShutdown() noexcept {
  impl_->accepting_messages.store(false);
  impl_->uia_query_worker.beginStop();
}

bool SelectionOverlay::joinUntil(
    std::chrono::steady_clock::time_point deadline) noexcept {
  return impl_->uia_query_worker.joinUntil(deadline);
}

std::string SelectionOverlay::diagnosticSnapshot() const {
  return impl_->uia_query_worker.diagnosticSnapshot();
}

void SelectionOverlay::finishShutdown() noexcept {
  hide();
  drainMessages();
}

void SelectionOverlay::shutdown() {
  beginShutdown();
  if (joinUntil((std::chrono::steady_clock::time_point::max)())) {
    finishShutdown();
  }
}

void SelectionOverlay::drainMessages() noexcept {
  impl_->messages.drain();
}

bool SelectionOverlay::isVisible() const noexcept {
  const HWND hwnd = reinterpret_cast<HWND>(overlay_hwnd_.load());
  return hwnd != nullptr && IsWindow(hwnd) != FALSE;
}

}  // namespace qingying
