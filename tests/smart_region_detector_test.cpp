#include "qingying/window/smart_region_diagnostics.hpp"
#include "smart_region_visual_cache.hpp"
#include "qingying/window/smart_region_window_snapshot_cache.hpp"
#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include <Windows.h>
#include <UIAutomation.h>
#include <oleacc.h>

#include <gtest/gtest.h>

#include "qingying/window/smart_region_detector.hpp"
#include "qingying/app/app_messages.hpp"
#include "known_content_locator.hpp"
#include "msaa_region_locator.hpp"
#include "uia_region_query_worker.hpp"
#include "uia_region_locator.hpp"
#include "window_query_helpers.h"

namespace qingying {
namespace {

struct BlockingUiaQueryContext
{
  std::mutex mutex;
  std::condition_variable condition;
  bool first_query_entered{false};
  bool release_first_query{false};
  int call_count{0};
};

struct CountingUiaQueryContext
{
  int call_count{0};
  bool succeed{true};
  bool browser_semantic_miss{false};
};

void runBlockingUiaQuery(
    const window_detail::UiaRegionQueryRequest& request,
    window_detail::UiaRegionQueryResult& result, void* context) noexcept
{
  BlockingUiaQueryContext* query_context =
      static_cast<BlockingUiaQueryContext*>(context);
  if (query_context == nullptr)
  {
    return;
  }

  {
    std::unique_lock<std::mutex> lock(query_context->mutex);
    ++query_context->call_count;
    if (query_context->call_count == 1)
    {
      query_context->first_query_entered = true;
      query_context->condition.notify_all();
      query_context->condition.wait(
          lock, [query_context]() {
            return query_context->release_first_query;
          });
    }
  }

  result.succeeded = true;
  result.candidate_count = 1;
  result.candidates[0] = {
      reinterpret_cast<std::uintptr_t>(request.root_window),
      request.request_id,
      {request.screen_point.x - 20, request.screen_point.y - 20,
       request.screen_point.x + 20, request.screen_point.y + 20},
      SmartRegionKind::KnownContent};
  result.candidates[0].source = SmartRegionDiagnosticSource::Uia;
  result.candidates[0].semantic = SmartRegionSemantic::ActionableControl;
}

void runCountingUiaQuery(
    const window_detail::UiaRegionQueryRequest& request,
    window_detail::UiaRegionQueryResult& result, void* context) noexcept
{
  CountingUiaQueryContext* query_context =
      static_cast<CountingUiaQueryContext*>(context);
  if (query_context == nullptr)
  {
    return;
  }
  ++query_context->call_count;
  result.succeeded = query_context->succeed;
  result.browser_semantic_miss = query_context->browser_semantic_miss;
  if (!result.succeeded)
  {
    return;
  }
  result.candidate_count = 1;
  result.candidates[0] = {
      reinterpret_cast<std::uintptr_t>(request.root_window), 7,
      {80, 80, 180, 180}, SmartRegionKind::KnownContent};
  result.candidates[0].source = SmartRegionDiagnosticSource::Uia;
  result.candidates[0].semantic = SmartRegionSemantic::ActionableControl;
}

bool waitForUiaQueryResult(window_detail::UiaRegionQueryWorker& worker,
                           window_detail::UiaRegionQueryResult& result)
{
  constexpr int MaximumAttempts = 500;
  for (int attempt = 0; attempt < MaximumAttempts; ++attempt)
  {
    if (worker.tryTakeLatest(result))
    {
      return true;
    }
    Sleep(1);
  }
  return false;
}

bool ensureWindowClass(const wchar_t* class_name)
{
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = DefWindowProcW;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = class_name;
  if (RegisterClassExW(&window_class) != 0) {
    return true;
  }
  return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

class TestWindowTree {
 public:
  TestWindowTree(const wchar_t* root_class, const wchar_t* content_class,
                 int content_x, int content_y, int content_width,
                 int content_height, DWORD content_style)
  {
    if (!ensureWindowClass(root_class) || !ensureWindowClass(content_class)) {
      return;
    }

    m_root = CreateWindowExW(
        WS_EX_TOOLWINDOW, root_class, L"", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        -10000, -10000, 1000, 800, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (m_root == nullptr) {
      return;
    }

    m_content = CreateWindowExW(
        0, content_class, L"", WS_CHILD | WS_VISIBLE | content_style,
        content_x, content_y, content_width, content_height, m_root, nullptr,
        GetModuleHandleW(nullptr), nullptr);
  }

  ~TestWindowTree()
  {
    if (m_content != nullptr) {
      DestroyWindow(m_content);
    }
    if (m_root != nullptr) {
      DestroyWindow(m_root);
    }
  }

  TestWindowTree(const TestWindowTree&) = delete;
  TestWindowTree& operator=(const TestWindowTree&) = delete;

  HWND root() const noexcept
  {
    return m_root;
  }

  HWND content() const noexcept
  {
    return m_content;
  }

  POINT contentCenter() const
  {
    const RECT rect = contentRect();
    return {(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
  }

  RECT contentRect() const
  {
    RECT rect{};
    if (m_content == nullptr || !GetClientRect(m_content, &rect)) {
      return RECT{};
    }
    POINT top_left{rect.left, rect.top};
    POINT bottom_right{rect.right, rect.bottom};
    if (!ClientToScreen(m_content, &top_left) ||
        !ClientToScreen(m_content, &bottom_right)) {
      return RECT{};
    }
    return {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
  }

  POINT rootClientPoint(int x, int y) const
  {
    POINT point{x, y};
    if (m_root != nullptr) {
      static_cast<void>(ClientToScreen(m_root, &point));
    }
    return point;
  }

 private:
  HWND m_root{nullptr};
  HWND m_content{nullptr};
};

TEST(SmartRegionHoverStabilizerTest, DelaysOnlyAChangeOfCandidate)
{
  const SmartRegionCandidate first{1, 11, {10, 20, 110, 220},
                                   SmartRegionKind::KnownContent};
  const SmartRegionCandidate second{1, 12, {20, 30, 120, 230},
                                    SmartRegionKind::KnownContent};
  SmartRegionHoverStabilizer stabilizer;

  EXPECT_TRUE(stabilizer.update(first, 100));
  EXPECT_FALSE(stabilizer.hasPendingCandidate());
  EXPECT_EQ(stabilizer.stableCandidate().target_window, 11U);
  EXPECT_FALSE(stabilizer.update(second, 120));
  EXPECT_TRUE(stabilizer.hasPendingCandidate());
  EXPECT_EQ(stabilizer.stableCandidate().target_window, 11U);
  EXPECT_TRUE(stabilizer.update(second, 200));
  EXPECT_FALSE(stabilizer.hasPendingCandidate());
  EXPECT_EQ(stabilizer.stableCandidate().target_window, 12U);
}

TEST(SmartRegionHoverStabilizerTest,
     CommitsAStableCandidateSwitchWithinFortyEightMilliseconds)
{
  const SmartRegionCandidate first{1, 11, {10, 20, 110, 220},
                                   SmartRegionKind::KnownContent};
  const SmartRegionCandidate second{1, 12, {20, 30, 120, 230},
                                    SmartRegionKind::KnownContent};
  SmartRegionHoverStabilizer stabilizer;

  ASSERT_TRUE(stabilizer.update(first, 100));
  EXPECT_FALSE(stabilizer.update(second, 120));
  EXPECT_TRUE(stabilizer.update(second, 168));
  EXPECT_EQ(stabilizer.stableCandidate().target_window, 12U);
}

TEST(SmartRegionHoverStabilizerTest,
     PromotesFirstDetailedCandidateWithoutFallbackDelay)
{
  SmartRegionCandidate fallback{1, 1, {0, 0, 1000, 800},
                                SmartRegionKind::Window};
  fallback.source = SmartRegionDiagnosticSource::Window;
  fallback.semantic = SmartRegionSemantic::Fallback;
  SmartRegionCandidate button{1, 12, {200, 180, 320, 220},
                              SmartRegionKind::KnownContent};
  button.source = SmartRegionDiagnosticSource::Uia;
  button.semantic = SmartRegionSemantic::ActionableControl;
  SmartRegionHoverStabilizer stabilizer;

  ASSERT_TRUE(stabilizer.update(fallback, 100));
  EXPECT_TRUE(stabilizer.update(button, 116));
  EXPECT_FALSE(stabilizer.hasPendingCandidate());
  EXPECT_EQ(stabilizer.stableCandidate().target_window, 12U);
}

TEST(SmartRegionHoverStabilizerTest,
     KeepsOverlappingBrowserWideVisualFallbackStable)
{
  SmartRegionCandidate first{1, 1, {0, 40, 1920, 120},
                             SmartRegionKind::KnownContent};
  first.source = SmartRegionDiagnosticSource::Visual;
  first.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate second{1, 1, {0, 80, 1920, 120},
                              SmartRegionKind::KnownContent};
  second.source = SmartRegionDiagnosticSource::Visual;
  second.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionHoverStabilizer browser_stabilizer;
  SmartRegionHoverStabilizer generic_stabilizer;

  ASSERT_TRUE(browser_stabilizer.update(first, 100, {600, 100}, true));
  EXPECT_FALSE(browser_stabilizer.update(second, 116, {600, 100}, true));
  EXPECT_FALSE(browser_stabilizer.hasPendingCandidate());
  EXPECT_EQ(browser_stabilizer.stableCandidate().rect.top, 40);

  ASSERT_TRUE(generic_stabilizer.update(first, 100, {600, 100}, false));
  EXPECT_FALSE(generic_stabilizer.update(second, 116, {600, 100}, false));
  EXPECT_TRUE(generic_stabilizer.hasPendingCandidate());
}

TEST(SmartRegionHoverStabilizerTest,
     RecordsPendingStartTimeForObservedSwitchDuration)
{
  const SmartRegionCandidate first{1, 11, {10, 20, 110, 220},
                                   SmartRegionKind::KnownContent};
  const SmartRegionCandidate second{1, 12, {20, 30, 120, 230},
                                    SmartRegionKind::KnownContent};
  SmartRegionHoverStabilizer stabilizer;

  ASSERT_TRUE(stabilizer.update(first, 100));
  ASSERT_FALSE(stabilizer.update(second, 120));
  EXPECT_EQ(stabilizer.pendingSinceMs(), 120U);
  ASSERT_TRUE(stabilizer.update(second, 168));
  EXPECT_EQ(stabilizer.pendingSinceMs(), 0U);
}

TEST(SmartRegionHoverStabilizerTest,
     PromotesAccessibilityActionableOverDetailedVisualCandidateImmediately)
{
  SmartRegionCandidate visual_region{1, 11, {0, 60, 384, 635},
                                     SmartRegionKind::KnownContent};
  visual_region.source = SmartRegionDiagnosticSource::Visual;
  visual_region.semantic = SmartRegionSemantic::ContentSurface;
  visual_region.visual_confidence = 90;
  SmartRegionCandidate browser_button{1, 12, {63, 69, 114, 120},
                                      SmartRegionKind::KnownContent};
  browser_button.source = SmartRegionDiagnosticSource::Msaa;
  browser_button.semantic = SmartRegionSemantic::ActionableControl;
  SmartRegionHoverStabilizer stabilizer;

  ASSERT_TRUE(stabilizer.update(visual_region, 100));
  EXPECT_TRUE(stabilizer.update(browser_button, 116));
  EXPECT_FALSE(stabilizer.hasPendingCandidate());
  EXPECT_EQ(stabilizer.stableCandidate().target_window, 12U);
}

TEST(SmartRegionHoverStabilizerTest,
     KeepsAccessibilityActionableCandidateWhenVisualFallbackContainsIt)
{
  SmartRegionCandidate visual_region{1, 11, {0, 60, 384, 635},
                                     SmartRegionKind::KnownContent};
  visual_region.source = SmartRegionDiagnosticSource::Visual;
  visual_region.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate browser_button{1, 12, {63, 69, 114, 120},
                                      SmartRegionKind::KnownContent};
  browser_button.source = SmartRegionDiagnosticSource::Msaa;
  browser_button.semantic = SmartRegionSemantic::ActionableControl;
  SmartRegionHoverStabilizer stabilizer;

  ASSERT_TRUE(stabilizer.update(visual_region, 100, {80, 80}));
  ASSERT_TRUE(stabilizer.update(browser_button, 116, {80, 80}));
  EXPECT_FALSE(stabilizer.update(visual_region, 132, {80, 80}));
  EXPECT_FALSE(stabilizer.update(visual_region, 180, {80, 80}));
  EXPECT_EQ(stabilizer.stableCandidate().target_window, 12U);
}

TEST(SmartRegionHoverStabilizerTest,
     AllowsVisualFallbackAfterPointerLeavesAccessibilityCandidate)
{
  SmartRegionCandidate visual_region{1, 11, {0, 60, 384, 635},
                                     SmartRegionKind::KnownContent};
  visual_region.source = SmartRegionDiagnosticSource::Visual;
  visual_region.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate browser_button{1, 12, {63, 69, 114, 120},
                                      SmartRegionKind::KnownContent};
  browser_button.source = SmartRegionDiagnosticSource::Msaa;
  browser_button.semantic = SmartRegionSemantic::ActionableControl;
  SmartRegionHoverStabilizer stabilizer;

  ASSERT_TRUE(stabilizer.update(visual_region, 100, {80, 80}));
  ASSERT_TRUE(stabilizer.update(browser_button, 116, {80, 80}));
  EXPECT_FALSE(stabilizer.update(visual_region, 132, {160, 160}));
  EXPECT_TRUE(stabilizer.update(visual_region, 180, {160, 160}));
  EXPECT_EQ(stabilizer.stableCandidate().target_window, 11U);
}

TEST(SmartRegionHoverStabilizerTest,
     UsesLatestPendingDetailedCandidateForClickSelection)
{
  SmartRegionCandidate first{1, 11, {100, 100, 300, 200},
                             SmartRegionKind::KnownContent};
  first.source = SmartRegionDiagnosticSource::Uia;
  first.semantic = SmartRegionSemantic::ActionableControl;
  SmartRegionCandidate second{1, 12, {120, 110, 320, 210},
                              SmartRegionKind::KnownContent};
  second.source = SmartRegionDiagnosticSource::Visual;
  second.semantic = SmartRegionSemantic::ContentSurface;
  second.visual_confidence = 90;
  SmartRegionHoverStabilizer stabilizer;

  ASSERT_TRUE(stabilizer.update(first, 100));
  ASSERT_FALSE(stabilizer.update(second, 116));
  ASSERT_TRUE(stabilizer.hasPendingCandidate());
  EXPECT_EQ(stabilizer.selectionCandidate().target_window, 12U);
}

TEST(SmartRegionHoverRenderGateTest,
     RequestsARenderOnlyWhenTheVisibleHoverRegionChanges)
{
  const SmartRegionCandidate first{1, 11, {10, 20, 110, 220},
                                   SmartRegionKind::KnownContent};
  const SmartRegionCandidate same_region_different_metadata{
      1, 11, {10, 20, 110, 220}, SmartRegionKind::KnownContent,
      SmartRegionDiagnosticSource::Visual, SmartRegionSemantic::ContentSurface};
  const SmartRegionCandidate second{1, 12, {20, 30, 120, 230},
                                    SmartRegionKind::KnownContent};
  SmartRegionHoverRenderGate gate;

  EXPECT_TRUE(gate.update(first, true));
  EXPECT_FALSE(gate.update(same_region_different_metadata, true));
  EXPECT_TRUE(gate.update(second, true));
  EXPECT_TRUE(gate.update(SmartRegionCandidate{}, false));
  EXPECT_FALSE(gate.update(SmartRegionCandidate{}, false));
}

TEST(SmartRegionUpdateGateTest, ProcessesFirstMoveAndCoalescesBurst)
{
  SmartRegionUpdateGate gate;

  EXPECT_TRUE(gate.shouldProcess(100));
  gate.markProcessed(100);
  EXPECT_FALSE(gate.shouldProcess(110));
  EXPECT_EQ(gate.remainingDelayMs(110), 6U);
  EXPECT_TRUE(gate.shouldProcess(116));
  gate.markProcessed(116);
  EXPECT_EQ(gate.remainingDelayMs(116), 16U);
}

TEST(SmartRegionUpdateGateTest, CountsNextIntervalFromUpdateStart)
{
  SmartRegionUpdateGate gate;

  gate.markStarted(100);
  EXPECT_FALSE(gate.shouldProcess(110));
  EXPECT_EQ(gate.remainingDelayMs(110), 6U);
  EXPECT_TRUE(gate.shouldProcess(116));

  gate.markStarted(116);
  EXPECT_EQ(gate.remainingDelayMs(120), 12U);
}

TEST(SmartRegionUpdateGateTest, ResetMakesTheNextMoveImmediate)
{
  SmartRegionUpdateGate gate;

  ASSERT_TRUE(gate.shouldProcess(100));
  gate.markProcessed(100);
  gate.reset();
  EXPECT_TRUE(gate.shouldProcess(101));
}

TEST(SmartRegionCandidateTest, RejectsEmptyAndAcceptsPointInside)
{
  const SmartRegionCandidate empty{};
  const SmartRegionCandidate candidate{7, 8, {10, 20, 110, 70},
                                       SmartRegionKind::ClientArea};

  EXPECT_FALSE(empty.valid());
  EXPECT_TRUE(candidate.valid());
  EXPECT_TRUE(candidate.contains(10, 20));
  EXPECT_FALSE(candidate.contains(110, 70));
}

TEST(KnownContentLocatorTest, ChromiumUsesRendererViewportOnly)
{
  TestWindowTree window(L"Chrome_WidgetWin_1", L"Chrome_RenderWidgetHostHWND",
                        40, 120, 800, 600, 0);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.content(), nullptr);

  SmartRegionCandidate candidate;
  ASSERT_TRUE(window_detail::locateKnownContent(
      window.root(), window.contentCenter(), candidate));
  EXPECT_EQ(candidate.kind, SmartRegionKind::KnownContent);
  EXPECT_EQ(candidate.target_window,
            reinterpret_cast<std::uintptr_t>(window.content()));
  EXPECT_EQ(candidate.rect.top, window.contentRect().top);
  EXPECT_FALSE(window_detail::locateKnownContent(
      window.root(), window.rootClientPoint(20, 20), candidate));
}

TEST(KnownContentLocatorTest, NotepadUsesMultilineEditor)
{
  TestWindowTree window(L"Notepad", L"Edit", 10, 60, 900, 650, ES_MULTILINE);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.content(), nullptr);

  SmartRegionCandidate candidate;
  ASSERT_TRUE(window_detail::locateKnownContent(
      window.root(), window.contentCenter(), candidate));
  EXPECT_EQ(candidate.kind, SmartRegionKind::KnownContent);
  EXPECT_EQ(candidate.target_window,
            reinterpret_cast<std::uintptr_t>(window.content()));
}

TEST(KnownContentLocatorTest, ExplorerUsesSmallestHitScrollSurface)
{
  TestWindowTree window(L"CabinetWClass", L"DirectUIHWND", 220, 100, 700,
                        620, 0);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.content(), nullptr);

  SmartRegionCandidate candidate;
  ASSERT_TRUE(window_detail::locateKnownContent(
      window.root(), window.contentCenter(), candidate));
  EXPECT_EQ(candidate.kind, SmartRegionKind::KnownContent);
  EXPECT_EQ(candidate.target_window,
            reinterpret_cast<std::uintptr_t>(window.content()));
}

TEST(KnownContentLocatorTest, UnsupportedApplicationReturnsNoKnownContent)
{
  TestWindowTree window(L"QingYingOrdinaryWindow", L"QingYingOrdinaryChild",
                        20, 20, 800, 600, 0);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.content(), nullptr);

  SmartRegionCandidate candidate;
  EXPECT_FALSE(window_detail::locateKnownContent(
      window.root(), window.contentCenter(), candidate));
  EXPECT_FALSE(candidate.valid());
}

TEST(SmartRegionCandidateTest, KnownContentRemainsMoreSpecificThanClientArea)
{
  const SmartRegionCandidate content{1, 2, {40, 120, 840, 720},
                                     SmartRegionKind::KnownContent};
  const SmartRegionCandidate client{1, 1, {0, 0, 1000, 800},
                                    SmartRegionKind::ClientArea};

  EXPECT_TRUE(content.valid());
  EXPECT_TRUE(client.valid());
  EXPECT_LT(content.rect.width() * content.rect.height(),
            client.rect.width() * client.rect.height());
  EXPECT_EQ(content.kind, SmartRegionKind::KnownContent);
}

TEST(SmartRegionDiagnosticTraceTest, RecordsOnlyWhenExplicitlyEnabled)
{
  SmartRegionDiagnosticTrace trace;
  SmartRegionDiagnosticEvent event;
  event.source = SmartRegionDiagnosticSource::KnownContent;
  event.rect = {20, 30, 220, 130};
  event.elapsed_ms = 7;

  EXPECT_FALSE(trace.enabled());
  EXPECT_FALSE(trace.record(event));
  EXPECT_FALSE(trace.hasLatestEvent());

  trace.setEnabled(true);
  EXPECT_TRUE(trace.record(event));
  EXPECT_TRUE(trace.hasLatestEvent());
  EXPECT_EQ(trace.latestEvent().source,
            SmartRegionDiagnosticSource::KnownContent);
  EXPECT_EQ(trace.latestEvent().elapsed_ms, 7U);
  EXPECT_STREQ(smartRegionDiagnosticSourceName(trace.latestEvent().source),
               L"known-content");
}

TEST(SmartRegionDetectorTest, RecordsWindowDetectionStageForAnInvalidPoint)
{
  SmartRegionDetector detector;
  SmartRegionDiagnosticTrace trace;
  SmartRegionCandidate candidate;
  trace.setEnabled(true);

  EXPECT_FALSE(detector.detectAt(-10000, -10000, candidate, &trace));
  ASSERT_TRUE(trace.hasLatestEvent());
  EXPECT_TRUE(trace.latestEvent().window_detection_attempted);
  EXPECT_LE(trace.latestEvent().window_detection_ms,
            trace.latestEvent().elapsed_ms);
}

TEST(SmartRegionDiagnosticTraceTest, RecordsObservedHoverTimingParts)
{
  SmartRegionDiagnosticTrace trace;
  SmartRegionDiagnosticEvent event;
  trace.setEnabled(true);
  ASSERT_TRUE(trace.record(event));

  EXPECT_TRUE(trace.recordOverlayRenderElapsed(24, 7));
  EXPECT_TRUE(trace.recordOverlayRenderCount(8));
  EXPECT_TRUE(trace.recordHoverInputDelay(12));
  EXPECT_TRUE(trace.recordStabilizationDelay(37));
  EXPECT_TRUE(trace.recordHoverMotion(-18, 6, 12, true));
  EXPECT_EQ(trace.latestEvent().overlay_render_ms, 24U);
  EXPECT_EQ(trace.latestEvent().overlay_render_count, 8U);
  EXPECT_EQ(trace.latestEvent().hover_input_delay_ms, 12U);
  EXPECT_EQ(trace.latestEvent().stabilization_delay_ms, 37U);
  EXPECT_EQ(trace.latestEvent().hover_motion_delta_x, -18);
  EXPECT_EQ(trace.latestEvent().hover_motion_delta_y, 6);
  EXPECT_EQ(trace.latestEvent().hover_motion_elapsed_ms, 12U);
  EXPECT_TRUE(trace.latestEvent().hover_motion_fast);
  EXPECT_FALSE(trace.latestEvent().msaa_lookup_attempted);
  EXPECT_EQ(trace.latestEvent().msaa_lookup_ms, 0U);
}

TEST(SmartRegionAsyncPresentationGateTest,
     DefersOnlyChromiumResultsDuringRecentFastRawMotion)
{
  SmartRegionAsyncPresentationGate gate;

  gate.recordRawMotion(100, 100, 100);
  gate.recordRawMotion(108, 101, 108);

  EXPECT_TRUE(gate.hasRecentFastMotion(108));
  EXPECT_TRUE(gate.shouldDeferAsyncResult(true, 108));
  EXPECT_FALSE(gate.shouldDeferAsyncResult(false, 108));
}

TEST(SmartRegionAsyncPresentationGateTest,
     ReleasesDeferredResultsAfterTheMotionQuietPeriod)
{
  SmartRegionAsyncPresentationGate gate;

  gate.recordRawMotion(100, 100, 100);
  gate.recordRawMotion(108, 101, 108);

  EXPECT_TRUE(gate.shouldDeferAsyncResult(
      true, 108 + SmartRegionAsyncPresentationGate::FastMotionHoldMs - 1));
  EXPECT_FALSE(gate.shouldDeferAsyncResult(
      true, 108 + SmartRegionAsyncPresentationGate::FastMotionHoldMs));
}

TEST(SmartRegionAsyncPresentationGateTest,
     IgnoresSmallOrSlowRawMotion)
{
  SmartRegionAsyncPresentationGate gate;

  gate.recordRawMotion(100, 100, 100);
  gate.recordRawMotion(102, 101, 108);
  EXPECT_FALSE(gate.hasRecentFastMotion(108));

  gate.recordRawMotion(110, 101,
                       108 + SmartRegionAsyncPresentationGate::
                                 FastMotionMaximumSampleIntervalMs +
                           1);
  EXPECT_FALSE(gate.hasRecentFastMotion(
      108 + SmartRegionAsyncPresentationGate::
                FastMotionMaximumSampleIntervalMs +
      1));
}

TEST(SmartRegionAsyncPresentationGateTest, ResetDropsTheRawMotionBurst)
{
  SmartRegionAsyncPresentationGate gate;

  gate.recordRawMotion(100, 100, 100);
  gate.recordRawMotion(108, 101, 108);
  ASSERT_TRUE(gate.hasRecentFastMotion(108));

  gate.reset();

  EXPECT_FALSE(gate.hasRecentFastMotion(108));
  EXPECT_EQ(gate.latestDeltaX(), 0);
  EXPECT_EQ(gate.latestDeltaY(), 0);
  EXPECT_EQ(gate.latestElapsedMs(), 0U);
}

TEST(SmartRegionDiagnosticTraceTest,
     AppendsWindowAndAsynchronousUiaQueryContext)
{
  SmartRegionDiagnosticTrace trace;
  SmartRegionDiagnosticEvent event;
  event.root_window = 42;
  event.cursor_x = 320;
  event.cursor_y = 180;
  event.process_id = 100;
  event.window_class[0] = L'C';
  event.window_class[1] = L'h';
  event.window_class[2] = L'r';
  event.window_class[3] = L'o';
  event.window_class[4] = L'm';
  event.window_class[5] = L'e';
  event.process_name[0] = L'c';
  event.process_name[1] = L'h';
  event.process_name[2] = L'r';
  event.process_name[3] = L'o';
  event.process_name[4] = L'm';
  event.process_name[5] = L'e';
  SmartRegionCandidate async_candidate;
  async_candidate.source = SmartRegionDiagnosticSource::Msaa;
  async_candidate.semantic = SmartRegionSemantic::ActionableControl;
  async_candidate.rect = {120, 8, 260, 40};
  async_candidate.accessibility_role = ROLE_SYSTEM_PAGETAB;
  async_candidate.accessibility_depth = 3;
  SmartRegionMsaaTraversalDiagnostic msaa_diagnostic{
      SmartRegionMsaaTraversalPath::AccessibleChildren,
      SmartRegionMsaaTraversalStopReason::CandidateFound, 24};
  msaa_diagnostic.filtered_node.reason =
      SmartRegionMsaaFilteredNodeReason::UnknownSemantic;
  msaa_diagnostic.filtered_node.role = ROLE_SYSTEM_BUTTONMENU;
  msaa_diagnostic.filtered_node.state = STATE_SYSTEM_FOCUSABLE;
  msaa_diagnostic.filtered_node.rect = {180, 12, 220, 44};
  msaa_diagnostic.filtered_node.accessibility_depth = 4;
  trace.setEnabled(true);
  ASSERT_TRUE(trace.record(event));

  ASSERT_TRUE(trace.recordAsyncUiaResult(
      9, 11, 17, 6, true, true, true, true, false,
      SmartRegionMaxUiaCandidates,
      true, true, false, SmartRegionAsyncDeferralReason::FastMotion,
      msaa_diagnostic, &async_candidate, 1));
  const SmartRegionDiagnosticEvent& recorded = trace.latestEvent();
  EXPECT_EQ(recorded.root_window, 42U);
  EXPECT_EQ(recorded.cursor_x, 320);
  EXPECT_EQ(recorded.cursor_y, 180);
  EXPECT_EQ(recorded.process_id, 100U);
  EXPECT_STREQ(recorded.window_class, L"Chrome");
  EXPECT_STREQ(recorded.process_name, L"chrome");
  EXPECT_TRUE(recorded.uia_async_result_received);
  EXPECT_EQ(recorded.uia_async_request_id, 9U);
  EXPECT_EQ(recorded.uia_async_elapsed_ms, 11U);
  EXPECT_EQ(recorded.uia_async_age_ms, 17U);
  EXPECT_EQ(recorded.uia_async_poll_delay_ms, 6U);
  EXPECT_TRUE(recorded.uia_async_result_succeeded);
  EXPECT_TRUE(recorded.uia_async_msaa_attempted);
  EXPECT_TRUE(recorded.uia_async_browser_semantic_miss);
  EXPECT_TRUE(recorded.uia_async_cache_hit);
  EXPECT_FALSE(recorded.uia_async_suppressed_by_cooldown);
  EXPECT_EQ(recorded.uia_async_candidate_count, SmartRegionMaxUiaCandidates);
  EXPECT_TRUE(recorded.uia_async_matches_current_request);
  EXPECT_TRUE(recorded.uia_async_result_applied);
  EXPECT_FALSE(recorded.uia_async_result_deferred);
  EXPECT_EQ(recorded.uia_async_deferral_reason,
            SmartRegionAsyncDeferralReason::FastMotion);
  EXPECT_EQ(recorded.uia_async_msaa_diagnostic.path,
            SmartRegionMsaaTraversalPath::AccessibleChildren);
  EXPECT_EQ(recorded.uia_async_msaa_diagnostic.stop_reason,
            SmartRegionMsaaTraversalStopReason::CandidateFound);
  EXPECT_EQ(recorded.uia_async_msaa_diagnostic.visited_child_count, 24U);
  EXPECT_EQ(recorded.uia_async_msaa_diagnostic.filtered_node.reason,
            SmartRegionMsaaFilteredNodeReason::UnknownSemantic);
  EXPECT_EQ(recorded.uia_async_msaa_diagnostic.filtered_node.role,
            ROLE_SYSTEM_BUTTONMENU);
  EXPECT_EQ(recorded.uia_async_msaa_diagnostic.filtered_node.state,
            STATE_SYSTEM_FOCUSABLE);
  EXPECT_EQ(recorded.uia_async_msaa_diagnostic.filtered_node.rect.left, 180);
  EXPECT_EQ(recorded.uia_async_msaa_diagnostic.filtered_node.rect.top, 12);
  EXPECT_EQ(recorded.uia_async_msaa_diagnostic.filtered_node.rect.right, 220);
  EXPECT_EQ(recorded.uia_async_msaa_diagnostic.filtered_node.rect.bottom, 44);
  EXPECT_EQ(
      recorded.uia_async_msaa_diagnostic.filtered_node.accessibility_depth,
      4U);
  ASSERT_EQ(recorded.uia_async_diagnostic_candidate_count, 1U);
  EXPECT_EQ(recorded.uia_async_candidates[0].source,
            SmartRegionDiagnosticSource::Msaa);
  EXPECT_EQ(recorded.uia_async_candidates[0].rect.left, 120);
  EXPECT_EQ(recorded.uia_async_candidates[0].rect.top, 8);
  EXPECT_EQ(recorded.uia_async_candidates[0].rect.right, 260);
  EXPECT_EQ(recorded.uia_async_candidates[0].rect.bottom, 40);
  EXPECT_EQ(recorded.uia_async_candidates[0].accessibility_role,
            static_cast<std::uint32_t>(ROLE_SYSTEM_PAGETAB));
  EXPECT_EQ(recorded.uia_async_candidates[0].accessibility_depth, 3U);
}

TEST(SmartRegionDiagnosticTraceTest, NamesMsaaTraversalPathAndStopReason)
{
  EXPECT_STREQ(smartRegionMsaaTraversalPathName(
                   SmartRegionMsaaTraversalPath::AccessibleChildren),
               L"AccessibleChildren");
  EXPECT_STREQ(smartRegionMsaaTraversalStopReasonName(
                   SmartRegionMsaaTraversalStopReason::TimeBudgetExhausted),
               L"TimeBudgetExhausted");
}

TEST(SmartRegionDiagnosticTraceTest, NamesMsaaFilteredNodeReason)
{
  EXPECT_STREQ(smartRegionMsaaFilteredNodeReasonName(
                   SmartRegionMsaaFilteredNodeReason::UnknownSemantic),
               L"UnknownSemantic");
}

TEST(SmartRegionWin32FixtureTest, CreatesCompactBrowserChromeLikeChildWindow)
{
  TestWindowTree window_tree(L"QingYingBrowserChromeFixtureRoot",
                             L"QingYingBrowserChromeFixtureChild", 24, 12,
                             18, 24, WS_TABSTOP);

  ASSERT_NE(window_tree.root(), nullptr);
  ASSERT_NE(window_tree.content(), nullptr);
  const RECT child_rect = window_tree.contentRect();
  EXPECT_EQ(child_rect.right - child_rect.left, 18);
  EXPECT_EQ(child_rect.bottom - child_rect.top, 24);
}

TEST(SmartRegionCandidateSelectorTest,
     PrefersBoundedActionableControlOverContainingContentSurface)
{
  const WindowRect owner_rect{0, 0, 1000, 800};
  SmartRegionCandidate list_pane{1, 11, {100, 100, 900, 700},
                                 SmartRegionKind::KnownContent};
  list_pane.source = SmartRegionDiagnosticSource::Uia;
  list_pane.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate list_item{1, 12, {120, 240, 720, 280},
                                 SmartRegionKind::KnownContent};
  list_item.source = SmartRegionDiagnosticSource::Uia;
  list_item.semantic = SmartRegionSemantic::ActionableControl;
  SmartRegionCandidate window{1, 1, owner_rect, SmartRegionKind::Window};
  window.source = SmartRegionDiagnosticSource::Window;
  window.semantic = SmartRegionSemantic::Fallback;
  const SmartRegionCandidate candidates[] = {list_pane, list_item, window};
  SmartRegionCandidate selected;

  ASSERT_TRUE(SmartRegionCandidateSelector::selectBest(
      candidates, std::size(candidates), 200, 260, owner_rect, selected));
  EXPECT_EQ(selected.target_window, 12U);
  EXPECT_EQ(selected.semantic, SmartRegionSemantic::ActionableControl);
}

TEST(SmartRegionVisualResultCacheTest,
     ReusesConfirmedLocalCandidateInsideItsBoundsAndNegativeWithinSameCell)
{
  const WindowRect owner_rect{0, 0, 1000, 800};
  SmartRegionCandidate visual_region{
      1, 2, {96, 96, 240, 240}, SmartRegionKind::KnownContent};
  visual_region.source = SmartRegionDiagnosticSource::Visual;
  visual_region.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionVisualResultCache cache;
  SmartRegionCandidate cached;
  bool found = false;

  cache.store(1, 101, owner_rect, 100, 100, &visual_region);
  EXPECT_TRUE(cache.lookup(1, 101, owner_rect, 107, 106, cached, found));
  EXPECT_TRUE(found);
  EXPECT_EQ(cached.rect.left, 96);
  EXPECT_TRUE(cache.lookup(1, 101, owner_rect, 224, 224, cached, found));
  EXPECT_TRUE(found);
  EXPECT_EQ(cached.rect.left, 96);
  EXPECT_FALSE(cache.lookup(1, 101, owner_rect, 240, 224, cached, found));

  cache.store(1, 101, owner_rect, 200, 200, nullptr);
  EXPECT_TRUE(cache.lookup(1, 101, owner_rect, 207, 207, cached, found));
  EXPECT_FALSE(found);
  EXPECT_FALSE(cached.valid());
  EXPECT_TRUE(cache.lookup(1, 101, owner_rect, 232, 232, cached, found));
  EXPECT_FALSE(found);
  EXPECT_FALSE(cache.lookup(2, 101, owner_rect, 207, 207, cached, found));
}

TEST(SmartRegionVisualResultCacheTest, ExpiresWithoutSleepingAndHasFixedStorage) {
  auto time = std::chrono::steady_clock::time_point{};
  SmartRegionVisualResultCache cache([](void* context) noexcept {
    return *static_cast<std::chrono::steady_clock::time_point*>(context);
  }, &time);
  const WindowRect owner{0, 0, 1000, 800};
  SmartRegionCandidate candidate{1, 2, {96, 96, 240, 240}, SmartRegionKind::KnownContent};
  candidate.source = SmartRegionDiagnosticSource::Visual;
  candidate.semantic = SmartRegionSemantic::ContentSurface;
  cache.store(1, 101, owner, 100, 100, &candidate);
  SmartRegionCandidate cached;
  bool found = false;
  ASSERT_TRUE(cache.lookup(1, 101, owner, 100, 100, cached, found));
  EXPECT_TRUE(found);
  time += std::chrono::seconds{2};
  EXPECT_FALSE(cache.lookup(1, 101, owner, 100, 100, cached, found));
  cache.store(1, 101, owner, 500, 500, nullptr);
  EXPECT_FALSE(cache.lookup(1, 101, owner, 100, 100, cached, found));
  EXPECT_EQ(cache.maximumCandidates(), 6u);
  EXPECT_EQ(cache.storageBytes(), sizeof(cache));
}

TEST(SmartRegionVisualResultCacheTest,
     ReusesWideBrowserFallbackOnlyInsideTheSameCell)
{
  const WindowRect owner_rect{0, 0, 1920, 120};
  SmartRegionCandidate visual_row{
      1, 1, {0, 80, 1920, 120}, SmartRegionKind::KnownContent};
  visual_row.source = SmartRegionDiagnosticSource::Visual;
  visual_row.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionVisualResultCache browser_cache;
  SmartRegionVisualResultCache generic_cache;
  SmartRegionCandidate cached;
  bool found = false;

  browser_cache.store(1, 101, owner_rect, 200, 96, &visual_row, 0, true);
  ASSERT_TRUE(
      browser_cache.lookup(1, 101, owner_rect, 230, 103, cached, found, true));
  EXPECT_TRUE(found);
  EXPECT_EQ(cached.rect.top, 80);
  EXPECT_FALSE(
      browser_cache.lookup(1, 101, owner_rect, 249, 103, cached, found, true));

  generic_cache.store(1, 101, owner_rect, 200, 96, &visual_row);
  EXPECT_FALSE(
      generic_cache.lookup(1, 101, owner_rect, 230, 103, cached, found));
}

TEST(SmartRegionVisualResultCacheTest,
     InvalidatesCachedCandidateWhenBackgroundIdentityChanges)
{
  const WindowRect owner_rect{0, 0, 1000, 800};
  SmartRegionCandidate visual_region{
      1, 2, {96, 96, 240, 240}, SmartRegionKind::KnownContent};
  visual_region.source = SmartRegionDiagnosticSource::Visual;
  visual_region.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionVisualResultCache cache;
  SmartRegionCandidate cached;
  bool found = false;

  cache.store(1, 101, owner_rect, 100, 100, &visual_region);

  EXPECT_FALSE(cache.lookup(1, 202, owner_rect, 107, 106, cached, found));
  EXPECT_FALSE(found);
  EXPECT_FALSE(cached.valid());
}

TEST(SmartRegionVisualResultCacheTest,
     DoesNotReuseCandidateBelowRequiredVisualConfidence)
{
  const WindowRect owner_rect{0, 0, 1000, 800};
  SmartRegionCandidate visual_region{
      1, 2, {96, 96, 240, 240}, SmartRegionKind::KnownContent};
  visual_region.source = SmartRegionDiagnosticSource::Visual;
  visual_region.semantic = SmartRegionSemantic::ContentSurface;
  visual_region.visual_confidence = 48;
  SmartRegionVisualResultCache cache;
  SmartRegionCandidate cached;
  bool found = false;

  cache.store(1, 101, owner_rect, 100, 100, &visual_region, 45);
  ASSERT_TRUE(cache.lookup(1, 101, owner_rect, 107, 106, cached, found));
  EXPECT_TRUE(found);

  cache.clear();
  cache.store(1, 101, owner_rect, 100, 100, &visual_region, 49);
  EXPECT_FALSE(cache.lookup(1, 101, owner_rect, 107, 106, cached, found));
}

TEST(SmartRegionCandidateSelectorTest,
     AllowsConfirmedWorkbenchVisualCandidateAtWorkbenchThreshold)
{
  const WindowRect owner_rect{0, 0, 1920, 1032};
  SmartRegionCandidate full_renderer{
      1, 2, owner_rect, SmartRegionKind::KnownContent};
  full_renderer.source = SmartRegionDiagnosticSource::KnownContent;
  full_renderer.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate terminal{
      1, 3, {371, 672, 1399, 897}, SmartRegionKind::KnownContent};
  terminal.source = SmartRegionDiagnosticSource::Visual;
  terminal.semantic = SmartRegionSemantic::ContentSurface;
  terminal.visual_confidence = 48;
  const SmartRegionCandidate candidates[] = {full_renderer, terminal};
  SmartRegionCandidate selected;

  ASSERT_TRUE(SmartRegionCandidateSelector::selectBest(
      candidates, std::size(candidates), 799, 845, owner_rect, selected, 45));
  EXPECT_EQ(selected.target_window, 3U);
  EXPECT_EQ(selected.source, SmartRegionDiagnosticSource::Visual);
}

TEST(SmartRegionVisualResultCacheTest,
     RetainsIndependentWorkbenchPanelsAcrossPanelTransitions)
{
  const WindowRect owner_rect{0, 0, 1600, 1000};
  SmartRegionCandidate editor{
      1, 2, {200, 80, 1300, 620}, SmartRegionKind::KnownContent};
  editor.source = SmartRegionDiagnosticSource::Visual;
  editor.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate terminal{
      1, 2, {200, 620, 1300, 920}, SmartRegionKind::KnownContent};
  terminal.source = SmartRegionDiagnosticSource::Visual;
  terminal.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionVisualResultCache cache;
  SmartRegionCandidate cached;
  bool found = false;

  cache.store(1, 101, owner_rect, 800, 300, &editor);
  cache.store(1, 101, owner_rect, 800, 760, &terminal);

  ASSERT_TRUE(cache.lookup(1, 101, owner_rect, 800, 300, cached, found));
  EXPECT_TRUE(found);
  EXPECT_EQ(cached.rect.top, 80);
  ASSERT_TRUE(cache.lookup(1, 101, owner_rect, 800, 760, cached, found));
  EXPECT_TRUE(found);
  EXPECT_EQ(cached.rect.top, 620);
}

TEST(SmartRegionCandidateSelectorTest,
     DoesNotTreatFallbackWrappersAsControlHierarchy)
{
  const WindowRect owner_rect{0, 0, 1000, 800};
  SmartRegionCandidate control{1, 11, {120, 120, 160, 160},
                               SmartRegionKind::KnownContent};
  control.source = SmartRegionDiagnosticSource::Uia;
  control.semantic = SmartRegionSemantic::ActionableControl;
  SmartRegionCandidate client{1, 1, {20, 20, 980, 780},
                              SmartRegionKind::ClientArea};
  client.source = SmartRegionDiagnosticSource::ClientArea;
  client.semantic = SmartRegionSemantic::Fallback;
  SmartRegionCandidate window{1, 1, owner_rect, SmartRegionKind::Window};
  window.source = SmartRegionDiagnosticSource::Window;
  window.semantic = SmartRegionSemantic::Fallback;
  const SmartRegionCandidate candidates[] = {control, client, window};
  SmartRegionCandidate selected;
  SmartRegionDiagnosticEvent diagnostic;

  ASSERT_TRUE(SmartRegionCandidateSelector::selectBest(
      candidates, std::size(candidates), 140, 140, owner_rect, selected,
      diagnostic));
  ASSERT_EQ(selected.target_window, 11U);
  ASSERT_EQ(diagnostic.candidate_count, 3U);
  EXPECT_EQ(diagnostic.candidates[0].hierarchy_score, 0);
}

TEST(SmartRegionCandidateSelectorTest,
     PrefersNamedParentControlOverUnnamedNestedControl)
{
  const WindowRect owner_rect{0, 0, 1000, 800};
  SmartRegionCandidate child{1, 11, {120, 120, 136, 140},
                             SmartRegionKind::KnownContent};
  child.source = SmartRegionDiagnosticSource::Uia;
  child.semantic = SmartRegionSemantic::ActionableControl;
  child.uia_metadata.available = true;
  child.uia_metadata.quality = SmartRegionUiaQuality::UnnamedActionable;
  SmartRegionCandidate parent{1, 12, {100, 100, 300, 260},
                              SmartRegionKind::KnownContent};
  parent.source = SmartRegionDiagnosticSource::Uia;
  parent.semantic = SmartRegionSemantic::ActionableControl;
  parent.uia_metadata.available = true;
  parent.uia_metadata.quality = SmartRegionUiaQuality::NamedActionable;
  const SmartRegionCandidate candidates[] = {child, parent};
  SmartRegionCandidate selected;
  SmartRegionDiagnosticEvent diagnostic;

  ASSERT_TRUE(SmartRegionCandidateSelector::selectBest(
      candidates, std::size(candidates), 128, 130, owner_rect, selected,
      diagnostic));
  EXPECT_EQ(selected.target_window, 12U);
  ASSERT_EQ(diagnostic.candidate_count, 2U);
  EXPECT_GT(diagnostic.candidates[1].quality_score,
            diagnostic.candidates[0].quality_score);
}

TEST(SmartRegionCandidateSelectorTest,
     RetainsDetectionMetadataWhileRecordingCandidateDiagnostics)
{
  const WindowRect owner_rect{0, 0, 1000, 800};
  SmartRegionCandidate content{1, 11, {100, 100, 900, 700},
                               SmartRegionKind::KnownContent};
  content.source = SmartRegionDiagnosticSource::KnownContent;
  content.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate selected;
  SmartRegionDiagnosticEvent diagnostic;
  diagnostic.window_detection_ms = 3;
  diagnostic.uia_lookup_ms = 11;
  diagnostic.known_content_lookup_ms = 7;
  diagnostic.visual_lookup_ms = 13;
  diagnostic.visual_edge_mask = 0x0F;

  ASSERT_TRUE(SmartRegionCandidateSelector::selectBest(
      &content, 1, 300, 300, owner_rect, selected, diagnostic));
  EXPECT_EQ(diagnostic.window_detection_ms, 3U);
  EXPECT_EQ(diagnostic.uia_lookup_ms, 11U);
  EXPECT_EQ(diagnostic.known_content_lookup_ms, 7U);
  EXPECT_EQ(diagnostic.visual_lookup_ms, 13U);
  EXPECT_EQ(diagnostic.visual_edge_mask, 0x0FU);
  ASSERT_EQ(diagnostic.candidate_count, 1U);
  EXPECT_TRUE(diagnostic.candidates[0].selected);
  EXPECT_EQ(diagnostic.candidates[0].area, 480000);
  EXPECT_EQ(diagnostic.candidates[0].owner_coverage_percent, 60U);
}

TEST(SmartRegionCandidateSelectorTest,
     PrefersVisualSidebarOverChromiumViewport)
{
  const WindowRect owner_rect{0, 0, 1200, 800};
  SmartRegionCandidate chromium_viewport{1, 2, {0, 100, 1200, 800},
                                         SmartRegionKind::KnownContent};
  chromium_viewport.source = SmartRegionDiagnosticSource::KnownContent;
  chromium_viewport.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate sidebar{1, 3, {0, 100, 256, 800},
                               SmartRegionKind::KnownContent};
  sidebar.source = SmartRegionDiagnosticSource::Visual;
  sidebar.semantic = SmartRegionSemantic::ContentSurface;
  sidebar.visual_confidence = 95;
  SmartRegionCandidate selected;
  const SmartRegionCandidate candidates[] = {chromium_viewport, sidebar};

  ASSERT_TRUE(SmartRegionCandidateSelector::selectBest(
      candidates, std::size(candidates), 80, 300, owner_rect, selected));
  EXPECT_EQ(selected.target_window, 3U);
  EXPECT_EQ(selected.source, SmartRegionDiagnosticSource::Visual);
}

TEST(SmartRegionCandidateSelectorTest,
     PrefersHighConfidenceVisualCardOverLargeUiaContentSurface)
{
  const WindowRect owner_rect{0, 0, 1200, 800};
  SmartRegionCandidate document{1, 2, {20, 20, 1180, 700},
                                SmartRegionKind::KnownContent};
  document.source = SmartRegionDiagnosticSource::Uia;
  document.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate card{1, 3, {400, 220, 680, 460},
                            SmartRegionKind::KnownContent};
  card.source = SmartRegionDiagnosticSource::Visual;
  card.semantic = SmartRegionSemantic::ContentSurface;
  card.visual_confidence = 95;
  const SmartRegionCandidate candidates[] = {document, card};
  SmartRegionCandidate selected;
  SmartRegionDiagnosticEvent diagnostic;

  ASSERT_TRUE(SmartRegionCandidateSelector::selectBest(
      candidates, std::size(candidates), 520, 320, owner_rect, selected,
      diagnostic));
  EXPECT_EQ(selected.target_window, 3U);
  EXPECT_EQ(selected.source, SmartRegionDiagnosticSource::Visual);
  EXPECT_GT(diagnostic.candidates[1].source_score, 0);
  EXPECT_GT(diagnostic.candidates[1].semantic_score, 0);
  EXPECT_GT(diagnostic.candidates[1].pointer_score, 0);
  EXPECT_GT(diagnostic.candidates[1].area_score, 0);
  EXPECT_GT(diagnostic.candidates[1].boundary_score, 0);
  EXPECT_GT(diagnostic.candidates[1].hierarchy_score, 0);
}

TEST(SmartRegionCandidateSelectorTest,
     MarksNearIdenticalLowerRankedCandidateAsDuplicate)
{
  const WindowRect owner_rect{0, 0, 1200, 800};
  SmartRegionCandidate uia_card{1, 2, {400, 220, 680, 460},
                                SmartRegionKind::KnownContent};
  uia_card.source = SmartRegionDiagnosticSource::Uia;
  uia_card.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate visual_card{1, 3, {401, 221, 681, 461},
                                   SmartRegionKind::KnownContent};
  visual_card.source = SmartRegionDiagnosticSource::Visual;
  visual_card.semantic = SmartRegionSemantic::ContentSurface;
  visual_card.visual_confidence = 95;
  const SmartRegionCandidate candidates[] = {uia_card, visual_card};
  SmartRegionCandidate selected;
  SmartRegionDiagnosticEvent diagnostic;

  ASSERT_TRUE(SmartRegionCandidateSelector::selectBest(
      candidates, std::size(candidates), 520, 320, owner_rect, selected,
      diagnostic));
  EXPECT_EQ(selected.target_window, 3U);
  EXPECT_EQ(diagnostic.candidates[0].rejection,
            SmartRegionCandidateRejection::Duplicate);
  EXPECT_TRUE(diagnostic.candidates[1].selected);
}

TEST(SmartRegionCandidateSelectorTest,
     DuplicateCandidateDoesNotEliminateAnotherDistinctCandidate)
{
  const WindowRect owner_rect{0, 0, 1200, 800};
  SmartRegionCandidate strongest{1, 2, {100, 100, 200, 200},
                                  SmartRegionKind::KnownContent};
  strongest.source = SmartRegionDiagnosticSource::Uia;
  strongest.semantic = SmartRegionSemantic::ActionableControl;
  SmartRegionCandidate duplicate{1, 3, {106, 100, 206, 200},
                                  SmartRegionKind::KnownContent};
  duplicate.source = SmartRegionDiagnosticSource::Msaa;
  duplicate.semantic = SmartRegionSemantic::ActionableControl;
  SmartRegionCandidate distinct{1, 4, {112, 100, 212, 200},
                                 SmartRegionKind::KnownContent};
  distinct.source = SmartRegionDiagnosticSource::KnownContent;
  distinct.semantic = SmartRegionSemantic::ContentSurface;
  const SmartRegionCandidate candidates[] = {strongest, duplicate, distinct};
  SmartRegionCandidate selected;
  SmartRegionDiagnosticEvent diagnostic;

  ASSERT_TRUE(SmartRegionCandidateSelector::selectBest(
      candidates, std::size(candidates), 150, 150, owner_rect, selected,
      diagnostic));
  EXPECT_EQ(selected.target_window, 2U);
  EXPECT_EQ(diagnostic.candidates[1].rejection,
            SmartRegionCandidateRejection::Duplicate);
  EXPECT_EQ(diagnostic.candidates[2].rejection,
            SmartRegionCandidateRejection::LowerScore);
}

TEST(SmartRegionCandidateSelectorTest,
     IgnoresCandidatesBeyondTheFixedCapacity)
{
  const WindowRect owner_rect{0, 0, 1200, 800};
  SmartRegionCandidate candidates[SmartRegionMaxCandidates + 1];
  for (std::size_t index = 0; index < SmartRegionMaxCandidates; ++index)
  {
    candidates[index] = {1, index + 1, owner_rect,
                         SmartRegionKind::Window};
    candidates[index].source = SmartRegionDiagnosticSource::Window;
    candidates[index].semantic = SmartRegionSemantic::Fallback;
  }
  candidates[SmartRegionMaxCandidates] = {
      1, 100, {400, 220, 680, 260}, SmartRegionKind::KnownContent};
  candidates[SmartRegionMaxCandidates].source =
      SmartRegionDiagnosticSource::Uia;
  candidates[SmartRegionMaxCandidates].semantic =
      SmartRegionSemantic::ActionableControl;
  SmartRegionCandidate selected;
  SmartRegionDiagnosticEvent diagnostic;

  ASSERT_TRUE(SmartRegionCandidateSelector::selectBest(
      candidates, std::size(candidates), 520, 240, owner_rect, selected,
      diagnostic));
  EXPECT_NE(selected.target_window, 100U);
  EXPECT_EQ(diagnostic.candidate_count, SmartRegionMaxCandidates);
}

TEST(SmartRegionCandidateSelectorTest,
     RejectsLowConfidenceVisualTextureInFavorOfKnownContent)
{
  const WindowRect owner_rect{0, 0, 1200, 800};
  SmartRegionCandidate content{1, 2, {0, 100, 1200, 800},
                               SmartRegionKind::KnownContent};
  content.source = SmartRegionDiagnosticSource::KnownContent;
  content.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate texture{1, 3, {420, 260, 524, 354},
                               SmartRegionKind::KnownContent};
  texture.source = SmartRegionDiagnosticSource::Visual;
  texture.semantic = SmartRegionSemantic::ContentSurface;
  texture.visual_confidence = 40;
  const SmartRegionCandidate candidates[] = {content, texture};
  SmartRegionCandidate selected;

  ASSERT_TRUE(SmartRegionCandidateSelector::selectBest(
      candidates, std::size(candidates), 460, 300, owner_rect, selected));
  EXPECT_EQ(selected.source, SmartRegionDiagnosticSource::KnownContent);
  EXPECT_EQ(selected.rect.left, 0);
  EXPECT_EQ(selected.rect.right, 1200);
}

TEST(SmartRegionCandidateSelectorTest,
     RejectsWindowSizedUiaContentSurfaceInFavorOfKnownContent)
{
  const WindowRect owner_rect{0, 0, 1200, 800};
  SmartRegionCandidate document{1, 1, {0, 0, 1200, 800},
                                SmartRegionKind::KnownContent};
  document.source = SmartRegionDiagnosticSource::Uia;
  document.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate content{1, 2, {0, 100, 1200, 800},
                               SmartRegionKind::KnownContent};
  content.source = SmartRegionDiagnosticSource::KnownContent;
  content.semantic = SmartRegionSemantic::ContentSurface;
  const SmartRegionCandidate candidates[] = {document, content};
  SmartRegionCandidate selected;
  SmartRegionDiagnosticEvent diagnostic;

  ASSERT_TRUE(SmartRegionCandidateSelector::selectBest(
      candidates, std::size(candidates), 500, 300, owner_rect, selected,
      diagnostic));
  EXPECT_EQ(selected.target_window, 2U);
  EXPECT_EQ(diagnostic.candidates[0].rejection,
            SmartRegionCandidateRejection::GenericTooLarge);
  EXPECT_TRUE(diagnostic.candidates[1].selected);
}

TEST(SmartRegionCandidateSelectorTest,
     FindsNoValidLocalUiaCandidateWhenOnlyWindowSizedDocumentExists)
{
  const WindowRect owner_rect{0, 0, 1200, 800};
  SmartRegionCandidate document{1, 1, owner_rect,
                                SmartRegionKind::KnownContent};
  document.source = SmartRegionDiagnosticSource::Uia;
  document.semantic = SmartRegionSemantic::ContentSurface;

  EXPECT_FALSE(SmartRegionCandidateSelector::hasValidLocalCandidate(
      &document, 1, 500, 300, owner_rect,
      SmartRegionDiagnosticSource::Uia));
}

TEST(SmartRegionCandidateSelectorTest,
     FindsValidLocalUiaCandidateForActionableControl)
{
  const WindowRect owner_rect{0, 0, 1200, 800};
  SmartRegionCandidate button{1, 1, {200, 180, 320, 220},
                              SmartRegionKind::KnownContent};
  button.source = SmartRegionDiagnosticSource::Uia;
  button.semantic = SmartRegionSemantic::ActionableControl;

  EXPECT_TRUE(SmartRegionCandidateSelector::hasValidLocalCandidate(
      &button, 1, 250, 200, owner_rect,
      SmartRegionDiagnosticSource::Uia));
}

TEST(SmartRegionCandidateSelectorTest,
     PrefersMsaaActionableWhenWindowSizedUiaContentIsRejected)
{
  const WindowRect owner_rect{0, 0, 1200, 800};
  SmartRegionCandidate document{1, 1, owner_rect,
                                SmartRegionKind::KnownContent};
  document.source = SmartRegionDiagnosticSource::Uia;
  document.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate button{1, 2, {200, 180, 320, 220},
                              SmartRegionKind::KnownContent};
  button.source = SmartRegionDiagnosticSource::Msaa;
  button.semantic = SmartRegionSemantic::ActionableControl;
  const SmartRegionCandidate candidates[] = {document, button};
  SmartRegionCandidate selected;
  SmartRegionDiagnosticEvent diagnostic;

  ASSERT_TRUE(SmartRegionCandidateSelector::selectBest(
      candidates, std::size(candidates), 250, 200, owner_rect, selected,
      diagnostic));
  EXPECT_EQ(selected.target_window, 2U);
  EXPECT_EQ(diagnostic.candidates[0].rejection,
            SmartRegionCandidateRejection::GenericTooLarge);
  EXPECT_TRUE(diagnostic.candidates[1].selected);
}

TEST(SmartRegionCandidateSelectorTest, RejectsInvalidCandidatesAndKeepsEditor)
{
  const WindowRect owner_rect{0, 0, 1000, 800};
  SmartRegionCandidate editor{1, 21, {80, 90, 900, 700},
                              SmartRegionKind::KnownContent};
  editor.source = SmartRegionDiagnosticSource::KnownContent;
  editor.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate outside{1, 22, {1010, 20, 1110, 120},
                               SmartRegionKind::KnownContent};
  outside.source = SmartRegionDiagnosticSource::Uia;
  outside.semantic = SmartRegionSemantic::ActionableControl;
  SmartRegionCandidate tiny{1, 23, {200, 200, 208, 208},
                            SmartRegionKind::KnownContent};
  tiny.source = SmartRegionDiagnosticSource::Uia;
  tiny.semantic = SmartRegionSemantic::ActionableControl;
  SmartRegionCandidate window{1, 1, owner_rect, SmartRegionKind::Window};
  window.source = SmartRegionDiagnosticSource::Window;
  window.semantic = SmartRegionSemantic::Fallback;
  const SmartRegionCandidate candidates[] = {outside, tiny, window, editor};
  SmartRegionCandidate selected;

  ASSERT_TRUE(SmartRegionCandidateSelector::selectBest(
      candidates, std::size(candidates), 204, 204, owner_rect, selected));
  EXPECT_EQ(selected.target_window, 21U);
  EXPECT_EQ(selected.semantic, SmartRegionSemantic::ContentSurface);
}

TEST(SmartRegionCandidateSelectorTest,
     ExplainsWhyEachCandidateWasRejectedOrSelected)
{
  const WindowRect owner_rect{0, 0, 1000, 800};
  SmartRegionCandidate too_small{1, 11, {200, 200, 208, 208},
                                 SmartRegionKind::KnownContent};
  too_small.source = SmartRegionDiagnosticSource::Uia;
  too_small.semantic = SmartRegionSemantic::ActionableControl;
  SmartRegionCandidate content{1, 12, {120, 120, 720, 680},
                               SmartRegionKind::KnownContent};
  content.source = SmartRegionDiagnosticSource::KnownContent;
  content.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate fallback{1, 1, owner_rect, SmartRegionKind::Window};
  fallback.source = SmartRegionDiagnosticSource::Window;
  fallback.semantic = SmartRegionSemantic::Fallback;
  const SmartRegionCandidate candidates[] = {too_small, content, fallback};
  SmartRegionCandidate selected;
  SmartRegionDiagnosticEvent diagnostic;

  ASSERT_TRUE(SmartRegionCandidateSelector::selectBest(
      candidates, std::size(candidates), 204, 204, owner_rect, selected,
      diagnostic));
  ASSERT_EQ(diagnostic.candidate_count, std::size(candidates));
  EXPECT_EQ(diagnostic.candidates[0].rejection,
            SmartRegionCandidateRejection::TooSmall);
  EXPECT_FALSE(diagnostic.candidates[0].selected);
  EXPECT_TRUE(diagnostic.candidates[1].selected);
  EXPECT_EQ(diagnostic.candidates[1].rejection,
            SmartRegionCandidateRejection::None);
  EXPECT_EQ(diagnostic.candidates[2].rejection,
            SmartRegionCandidateRejection::LowerScore);
  EXPECT_STREQ(smartRegionCandidateRejectionName(
                   diagnostic.candidates[2].rejection),
               L"lower-score");
}

TEST(UiaRegionQueryWorkerTest, SubmitDoesNotBlockAndBurstKeepsLatestRequest)
{
  BlockingUiaQueryContext context;
  window_detail::UiaRegionQueryWorker worker(&runBlockingUiaQuery, &context);
  ASSERT_TRUE(worker.start());

  const std::uint64_t begin_ms = GetTickCount64();
  EXPECT_TRUE(worker.request({1, reinterpret_cast<HWND>(1), {100, 100},
                              {0, 0, 1000, 800}, 100}));
  EXPECT_LT(GetTickCount64() - begin_ms, 50U);

  bool entered = false;
  {
    std::unique_lock<std::mutex> lock(context.mutex);
    entered = context.condition.wait_for(
        lock, std::chrono::seconds(1),
        [&context]() { return context.first_query_entered; });
  }
  EXPECT_TRUE(entered);

  EXPECT_TRUE(worker.request({2, reinterpret_cast<HWND>(1), {120, 120},
                              {0, 0, 1000, 800}, 110}));
  EXPECT_TRUE(worker.request({3, reinterpret_cast<HWND>(1), {140, 140},
                              {0, 0, 1000, 800}, 120}));
  {
    std::lock_guard<std::mutex> lock(context.mutex);
    context.release_first_query = true;
  }
  context.condition.notify_all();

  window_detail::UiaRegionQueryResult result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, result));
  EXPECT_EQ(result.request_id, 3U);
  ASSERT_EQ(result.candidate_count, 1U);
  EXPECT_EQ(result.candidates[0].target_window, 3U);
  EXPECT_EQ(context.call_count, 2);
}

TEST(UiaRegionQueryWorkerTest,
     JoinUntilReportsBlockedProviderAndRetainsWorkerOwnership)
{
  BlockingUiaQueryContext context;
  window_detail::UiaRegionQueryWorker worker(&runBlockingUiaQuery, &context);
  ASSERT_TRUE(worker.start());
  ASSERT_TRUE(worker.request({1, reinterpret_cast<HWND>(1), {100, 100},
                              {0, 0, 1000, 800}, 100}));

  {
    std::unique_lock<std::mutex> lock(context.mutex);
    ASSERT_TRUE(context.condition.wait_for(
        lock, std::chrono::seconds(1),
        [&context]() { return context.first_query_entered; }));
  }

  worker.beginStop();
  EXPECT_FALSE(worker.joinUntil(
      std::chrono::steady_clock::now() + std::chrono::milliseconds(20)));
  const std::string snapshot = worker.diagnosticSnapshot();
  EXPECT_NE(snapshot.find("thread=uia_query_worker"), std::string::npos);
  EXPECT_NE(snapshot.find("request_id=1"), std::string::npos);
  EXPECT_NE(snapshot.find("last_progress=provider_callback"),
            std::string::npos);

  {
    std::lock_guard<std::mutex> lock(context.mutex);
    context.release_first_query = true;
  }
  context.condition.notify_all();
  EXPECT_TRUE(worker.joinUntil(
      std::chrono::steady_clock::now() + std::chrono::seconds(1)));
}

TEST(UiaRegionQueryWorkerTest, ClearDiscardsAnInFlightResult)
{
  BlockingUiaQueryContext context;
  window_detail::UiaRegionQueryWorker worker(&runBlockingUiaQuery, &context);
  ASSERT_TRUE(worker.start());
  ASSERT_TRUE(worker.request({1, reinterpret_cast<HWND>(1), {100, 100},
                              {0, 0, 1000, 800}, 100}));

  {
    std::unique_lock<std::mutex> lock(context.mutex);
    ASSERT_TRUE(context.condition.wait_for(
        lock, std::chrono::seconds(1),
        [&context]() { return context.first_query_entered; }));
  }

  worker.clear();
  {
    std::lock_guard<std::mutex> lock(context.mutex);
    context.release_first_query = true;
  }
  context.condition.notify_all();

  for (int attempt = 0; attempt < 100; ++attempt)
  {
    if (!worker.hasPendingWork())
    {
      break;
    }
    Sleep(1);
  }
  window_detail::UiaRegionQueryResult result;
  EXPECT_FALSE(worker.tryTakeLatest(result));
}

TEST(SmartRegionCandidateCollectionTest,
     KeepsMeaningfulHierarchyAndCyclesInBothDirections)
{
  SmartRegionCandidate control{1, 11, {120, 120, 240, 180},
                               SmartRegionKind::KnownContent};
  control.source = SmartRegionDiagnosticSource::Uia;
  control.semantic = SmartRegionSemantic::ActionableControl;
  SmartRegionCandidate content{1, 12, {80, 80, 700, 600},
                               SmartRegionKind::KnownContent};
  content.source = SmartRegionDiagnosticSource::KnownContent;
  content.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate duplicate = control;
  duplicate.source = SmartRegionDiagnosticSource::Msaa;
  SmartRegionCandidate client{1, 1, {20, 20, 980, 780},
                              SmartRegionKind::ClientArea};
  client.source = SmartRegionDiagnosticSource::ClientArea;
  client.semantic = SmartRegionSemantic::Fallback;
  SmartRegionCandidate window{1, 1, {0, 0, 1000, 800},
                              SmartRegionKind::Window};
  window.source = SmartRegionDiagnosticSource::Window;
  window.semantic = SmartRegionSemantic::Fallback;
  const SmartRegionCandidate candidates[] = {
      window, content, duplicate, client, control};
  SmartRegionCandidateCollection collection;

  collection.replace(candidates, std::size(candidates), 160, 150,
                     {0, 0, 1000, 800}, control);

  ASSERT_EQ(collection.count(), 4U);
  EXPECT_EQ(collection.current().target_window, 11U);
  ASSERT_TRUE(collection.cycle(1));
  EXPECT_EQ(collection.current().target_window, 12U);
  ASSERT_TRUE(collection.cycle(1));
  EXPECT_EQ(collection.current().kind, SmartRegionKind::ClientArea);
  ASSERT_TRUE(collection.cycle(1));
  EXPECT_EQ(collection.current().kind, SmartRegionKind::Window);
  ASSERT_TRUE(collection.cycle(-1));
  EXPECT_EQ(collection.current().kind, SmartRegionKind::ClientArea);
}

TEST(SmartRegionCandidateCollectionTest,
     PlacesGenericParentBeforeContentSurfaceWhenCycling)
{
  SmartRegionCandidate control{1, 11, {200, 200, 280, 240},
                               SmartRegionKind::KnownContent};
  control.source = SmartRegionDiagnosticSource::Uia;
  control.semantic = SmartRegionSemantic::ActionableControl;
  SmartRegionCandidate parent{1, 12, {80, 80, 900, 700},
                              SmartRegionKind::KnownContent};
  parent.source = SmartRegionDiagnosticSource::Uia;
  parent.semantic = SmartRegionSemantic::ContentSurface;
  parent.uia_metadata.available = true;
  parent.uia_metadata.quality = SmartRegionUiaQuality::GenericContainer;
  SmartRegionCandidate content{1, 13, {120, 120, 700, 560},
                               SmartRegionKind::KnownContent};
  content.source = SmartRegionDiagnosticSource::Visual;
  content.semantic = SmartRegionSemantic::ContentSurface;
  content.visual_confidence = 95;
  content.uia_metadata.available = true;
  content.uia_metadata.quality = SmartRegionUiaQuality::ContentSurface;
  SmartRegionCandidate client{1, 1, {20, 20, 980, 780},
                              SmartRegionKind::ClientArea};
  client.source = SmartRegionDiagnosticSource::ClientArea;
  client.semantic = SmartRegionSemantic::Fallback;
  SmartRegionCandidate window{1, 1, {0, 0, 1000, 800},
                              SmartRegionKind::Window};
  window.source = SmartRegionDiagnosticSource::Window;
  window.semantic = SmartRegionSemantic::Fallback;
  const SmartRegionCandidate candidates[] = {
      window, content, client, parent, control};
  SmartRegionCandidateCollection collection;

  collection.replace(candidates, std::size(candidates), 220, 220,
                     {0, 0, 1000, 800}, control);

  ASSERT_EQ(collection.count(), 5U);
  ASSERT_TRUE(collection.cycle(1));
  EXPECT_EQ(collection.current().target_window, 12U);
  ASSERT_TRUE(collection.cycle(1));
  EXPECT_EQ(collection.current().target_window, 13U);
}

TEST(SmartRegionCandidateCollectionTest,
     ClearDropsTheCandidateChainAndDisablesCycling)
{
  SmartRegionCandidate candidate{1, 2, {20, 20, 220, 120},
                                 SmartRegionKind::KnownContent};
  candidate.source = SmartRegionDiagnosticSource::KnownContent;
  candidate.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidateCollection collection;
  collection.replace(&candidate, 1, 40, 40, {0, 0, 800, 600}, candidate);
  ASSERT_EQ(collection.count(), 1U);

  collection.clear();

  EXPECT_TRUE(collection.empty());
  EXPECT_FALSE(collection.current().valid());
  EXPECT_FALSE(collection.cycle(1));
}

TEST(SmartRegionCandidateCollectionTest,
     NearSizedWindowSelectionDoesNotStartAtClientArea)
{
  SmartRegionCandidate client{1, 1, {20, 20, 980, 780},
                              SmartRegionKind::ClientArea};
  client.source = SmartRegionDiagnosticSource::ClientArea;
  client.semantic = SmartRegionSemantic::Fallback;
  SmartRegionCandidate window{1, 1, {0, 0, 1000, 800},
                              SmartRegionKind::Window};
  window.source = SmartRegionDiagnosticSource::Window;
  window.semantic = SmartRegionSemantic::Fallback;
  const SmartRegionCandidate candidates[] = {client, window};
  SmartRegionCandidateCollection collection;

  collection.replace(candidates, std::size(candidates), 200, 200,
                     {0, 0, 1000, 800}, window);

  ASSERT_EQ(collection.count(), 2U);
  EXPECT_EQ(collection.current().kind, SmartRegionKind::Window);
}

TEST(UiaRegionQueryWorkerTest, ReusesRecentResultForNearbyPointer)
{
  CountingUiaQueryContext context;
  window_detail::UiaRegionQueryWorker worker(&runCountingUiaQuery, &context);
  ASSERT_TRUE(worker.start());
  ASSERT_TRUE(worker.request({1, reinterpret_cast<HWND>(1), {100, 100},
                              {0, 0, 1000, 800}, 100}));
  window_detail::UiaRegionQueryResult first_result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, first_result));

  ASSERT_TRUE(worker.request({2, reinterpret_cast<HWND>(1), {103, 102},
                              {0, 0, 1000, 800}, 140}));
  window_detail::UiaRegionQueryResult cached_result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, cached_result));
  EXPECT_EQ(cached_result.request_id, 2U);
  EXPECT_EQ(cached_result.requested_at_ms, 100U);
  EXPECT_TRUE(cached_result.cache_hit);
  EXPECT_EQ(context.call_count, 1);
}

TEST(UiaRegionQueryWorkerTest, CoolsDownRepeatedFailureForTheSameWindow)
{
  CountingUiaQueryContext context;
  context.succeed = false;
  window_detail::UiaRegionQueryWorker worker(&runCountingUiaQuery, &context);
  ASSERT_TRUE(worker.start());
  ASSERT_TRUE(worker.request({1, reinterpret_cast<HWND>(1), {100, 100},
                              {0, 0, 1000, 800}, 100}));
  window_detail::UiaRegionQueryResult first_result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, first_result));
  EXPECT_FALSE(first_result.succeeded);

  ASSERT_TRUE(worker.request({2, reinterpret_cast<HWND>(1), {102, 102},
                              {0, 0, 1000, 800}, 140}));
  window_detail::UiaRegionQueryResult cooled_result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, cooled_result));
  EXPECT_EQ(cooled_result.request_id, 2U);
  EXPECT_TRUE(cooled_result.suppressed_by_cooldown);
  EXPECT_EQ(context.call_count, 1);
}

TEST(UiaRegionQueryWorkerTest,
     RetriesBrowserSemanticMissAtTheNextHoverInterval)
{
  CountingUiaQueryContext context;
  context.succeed = false;
  context.browser_semantic_miss = true;
  window_detail::UiaRegionQueryWorker worker(&runCountingUiaQuery, &context);
  ASSERT_TRUE(worker.start());
  ASSERT_TRUE(worker.request({1, reinterpret_cast<HWND>(1), {100, 100},
                              {0, 0, 1000, 800}, 100}));
  window_detail::UiaRegionQueryResult first_result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, first_result));
  EXPECT_TRUE(first_result.browser_semantic_miss);

  ASSERT_TRUE(worker.request({2, reinterpret_cast<HWND>(1), {102, 102},
                              {0, 0, 1000, 800}, 116}));
  window_detail::UiaRegionQueryResult retried_result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, retried_result));
  EXPECT_FALSE(retried_result.suppressed_by_cooldown);
  EXPECT_EQ(context.call_count, 2);
}

TEST(UiaRegionQueryWorkerTest, StampsCompletionTimeBeforePublishingResult)
{
  CountingUiaQueryContext context;
  window_detail::UiaRegionQueryWorker worker(&runCountingUiaQuery, &context);
  ASSERT_TRUE(worker.start());
  const std::uint64_t requested_at_ms = GetTickCount64();
  ASSERT_TRUE(worker.request({1, reinterpret_cast<HWND>(1), {100, 100},
                              {0, 0, 1000, 800}, requested_at_ms}));

  window_detail::UiaRegionQueryResult result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, result));
  EXPECT_GE(result.completed_at_ms, requested_at_ms);
}

TEST(UiaRegionQueryWorkerTest,
     ReusesRecentResultWhenPointerMovesWithinTheSameCandidate)
{
  CountingUiaQueryContext context;
  window_detail::UiaRegionQueryWorker worker(&runCountingUiaQuery, &context);
  ASSERT_TRUE(worker.start());
  ASSERT_TRUE(worker.request({1, reinterpret_cast<HWND>(1), {100, 100},
                              {0, 0, 1000, 800}, 100}));
  window_detail::UiaRegionQueryResult first_result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, first_result));

  ASSERT_TRUE(worker.request({2, reinterpret_cast<HWND>(1), {160, 160},
                              {0, 0, 1000, 800}, 140}));
  window_detail::UiaRegionQueryResult cached_result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, cached_result));
  EXPECT_TRUE(cached_result.cache_hit);
  EXPECT_EQ(context.call_count, 1);
}

TEST(UiaRegionQueryWorkerTest, DoesNotReuseResultAfterOwnerBoundsChange)
{
  CountingUiaQueryContext context;
  window_detail::UiaRegionQueryWorker worker(&runCountingUiaQuery, &context);
  ASSERT_TRUE(worker.start());
  ASSERT_TRUE(worker.request({1, reinterpret_cast<HWND>(1), {100, 100},
                              {0, 0, 1000, 800}, 100}));
  window_detail::UiaRegionQueryResult first_result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, first_result));

  ASSERT_TRUE(worker.request({2, reinterpret_cast<HWND>(1), {102, 102},
                              {0, 0, 960, 760}, 140}));
  window_detail::UiaRegionQueryResult refreshed_result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, refreshed_result));
  EXPECT_FALSE(refreshed_result.cache_hit);
  EXPECT_EQ(context.call_count, 2);
}

TEST(UiaRegionQueryWorkerTest,
     DoesNotCooldownFailureForAnotherControlInTheSameWindow)
{
  CountingUiaQueryContext context;
  context.succeed = false;
  window_detail::UiaRegionQueryWorker worker(&runCountingUiaQuery, &context);
  ASSERT_TRUE(worker.start());
  ASSERT_TRUE(worker.request({1, reinterpret_cast<HWND>(1), {100, 100},
                              {0, 0, 1000, 800}, 100}));
  window_detail::UiaRegionQueryResult first_result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, first_result));
  EXPECT_FALSE(first_result.succeeded);

  ASSERT_TRUE(worker.request({2, reinterpret_cast<HWND>(1), {240, 100},
                              {0, 0, 1000, 800}, 140}));
  window_detail::UiaRegionQueryResult other_control_result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, other_control_result));
  EXPECT_FALSE(other_control_result.suppressed_by_cooldown);
  EXPECT_EQ(context.call_count, 2);
}

TEST(UiaRegionQueryWorkerTest, AccessibilityCandidateCompetesWithFastFallback)
{
  window_detail::UiaRegionQueryResult result;
  result.request_id = 1;
  result.root_window = reinterpret_cast<HWND>(1);
  result.screen_point = {150, 150};
  result.owner_rect = {0, 0, 1000, 800};
  result.succeeded = true;
  result.candidate_count = 1;
  result.candidates[0] = {
      1, 2, {100, 100, 220, 190}, SmartRegionKind::KnownContent};
  result.candidates[0].source = SmartRegionDiagnosticSource::Uia;
  result.candidates[0].semantic = SmartRegionSemantic::ActionableControl;
  SmartRegionCandidate fast_fallback{
      1, 1, {0, 0, 1000, 800}, SmartRegionKind::Window};
  fast_fallback.source = SmartRegionDiagnosticSource::Window;
  fast_fallback.semantic = SmartRegionSemantic::Fallback;
  SmartRegionCandidate selected;

  ASSERT_TRUE(window_detail::selectUiaQueryCandidate(
      result, fast_fallback, result.screen_point, result.owner_rect,
      selected));
  EXPECT_EQ(selected.target_window, 2U);
  EXPECT_EQ(selected.source, SmartRegionDiagnosticSource::Uia);
}

TEST(UiaRegionQueryWorkerTest,
     ChromeAddressBarActionableBeatsHighConfidenceVisualToolbarRow)
{
  window_detail::UiaRegionQueryResult result;
  result.request_id = 1;
  result.root_window = reinterpret_cast<HWND>(1);
  result.screen_point = {1410, 82};
  result.owner_rect = {0, 0, 2560, 1528};
  result.succeeded = true;
  result.candidate_count = 1;
  result.candidates[0] = {
      1, 2, {237, 77, 1887, 114}, SmartRegionKind::KnownContent};
  result.candidates[0].source = SmartRegionDiagnosticSource::Msaa;
  result.candidates[0].semantic = SmartRegionSemantic::ActionableControl;
  result.candidates[0].accessibility_role = ROLE_SYSTEM_TEXT;
  SmartRegionCandidate visual_toolbar{
      1, 1, {0, 69, 2560, 120}, SmartRegionKind::KnownContent};
  visual_toolbar.source = SmartRegionDiagnosticSource::Visual;
  visual_toolbar.semantic = SmartRegionSemantic::ContentSurface;
  visual_toolbar.visual_confidence = 100;
  SmartRegionCandidate selected;

  ASSERT_TRUE(window_detail::selectUiaQueryCandidate(
      result, visual_toolbar, result.screen_point, result.owner_rect,
      selected));
  EXPECT_EQ(selected.target_window, 2U);
  EXPECT_EQ(selected.source, SmartRegionDiagnosticSource::Msaa);
}

TEST(UiaRegionQueryWorkerTest,
     PreservesFastFallbackWhenUiaPathFillsTheFixedCandidateCapacity)
{
  const WindowRect owner_rect{0, 0, 1000, 800};
  window_detail::UiaRegionQueryResult result;
  result.request_id = 1;
  result.root_window = reinterpret_cast<HWND>(1);
  result.screen_point = {204, 204};
  result.owner_rect = owner_rect;
  result.succeeded = true;
  result.candidate_count = SmartRegionMaxCandidates;
  for (std::size_t index = 0; index < result.candidate_count; ++index)
  {
    result.candidates[index] = {
        1, index + 1, {200, 200, 208, 208}, SmartRegionKind::KnownContent};
    result.candidates[index].source = SmartRegionDiagnosticSource::Uia;
    result.candidates[index].semantic =
        SmartRegionSemantic::ActionableControl;
  }
  SmartRegionCandidate fast_fallback{
      1, 100, {100, 100, 700, 600}, SmartRegionKind::KnownContent};
  fast_fallback.source = SmartRegionDiagnosticSource::KnownContent;
  fast_fallback.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate selected;

  ASSERT_TRUE(window_detail::selectUiaQueryCandidate(
      result, fast_fallback, result.screen_point, result.owner_rect,
      selected));
  EXPECT_EQ(selected.target_window, 100U);
  EXPECT_EQ(selected.source, SmartRegionDiagnosticSource::KnownContent);
}

TEST(UiaRegionQueryWorkerTest,
     RetainsUiaAndMsaaWithinTheReservedFallbackCapacity)
{
  SmartRegionCandidate input[SmartRegionMaxCandidates];
  for (std::size_t index = 0; index < SmartRegionMaxUiaCandidates; ++index)
  {
    input[index] = {
        1, index + 1, {100, 100, 180, 140}, SmartRegionKind::KnownContent};
    input[index].source = SmartRegionDiagnosticSource::Uia;
    input[index].semantic = SmartRegionSemantic::ActionableControl;
  }
  input[SmartRegionMaxUiaCandidates] = {
      1, 99, {200, 200, 300, 240}, SmartRegionKind::KnownContent};
  input[SmartRegionMaxUiaCandidates].source = SmartRegionDiagnosticSource::Msaa;
  input[SmartRegionMaxUiaCandidates].semantic =
      SmartRegionSemantic::ActionableControl;
  for (std::size_t index = SmartRegionMaxAccessibilityCandidates;
       index < SmartRegionMaxCandidates; ++index)
  {
    input[index] = {
        1, index + 1, {400, 400, 408, 408}, SmartRegionKind::KnownContent};
    input[index].source = SmartRegionDiagnosticSource::Uia;
    input[index].semantic = SmartRegionSemantic::ActionableControl;
  }
  SmartRegionCandidate retained[SmartRegionMaxCandidates];

  const std::size_t retained_count =
      window_detail::retainAccessibilityCandidates(
          input, std::size(input), retained, std::size(retained));

  ASSERT_EQ(retained_count, SmartRegionMaxAccessibilityCandidates);
  EXPECT_EQ(retained[0].source, SmartRegionDiagnosticSource::Uia);
  EXPECT_EQ(retained[3].source, SmartRegionDiagnosticSource::Uia);
  EXPECT_EQ(retained[4].source, SmartRegionDiagnosticSource::Msaa);
  EXPECT_LT(retained_count, SmartRegionMaxCandidates);
}

TEST(UiaRegionQueryWorkerTest, PreservesWorkerVisualPolicyInSelectionAndCycling) {
  window_detail::UiaRegionQueryResult result;
  result.candidate_count = 2;
  result.candidates[0] = {1, 2, {100, 100, 300, 200}, SmartRegionKind::KnownContent};
  result.candidates[0].source = SmartRegionDiagnosticSource::Visual;
  result.candidates[0].semantic = SmartRegionSemantic::ContentSurface;
  result.candidates[0].visual_confidence = 48;
  result.candidates[1] = {1, 1, {0, 0, 800, 600}, SmartRegionKind::ClientArea};
  result.candidates[1].source = SmartRegionDiagnosticSource::ClientArea;
  result.candidates[1].semantic = SmartRegionSemantic::Fallback;
  SmartRegionCandidate selected;
  ASSERT_TRUE(window_detail::selectUiaQueryCandidate(result, {}, {120, 120},
      {0, 0, 800, 600}, selected));
  EXPECT_EQ(selected.source, SmartRegionDiagnosticSource::ClientArea);
  result.minimum_visual_confidence = 45;
  ASSERT_TRUE(window_detail::selectUiaQueryCandidate(result, {}, {120, 120},
      {0, 0, 800, 600}, selected));
  EXPECT_EQ(selected.source, SmartRegionDiagnosticSource::Visual);
  SmartRegionCandidateCollection collection;
  collection.replace(result.candidates, 2, 120, 120, {0, 0, 800, 600}, selected,
                     result.minimum_visual_confidence);
  EXPECT_EQ(collection.count(), 2u);
  EXPECT_EQ(collection.current().source, SmartRegionDiagnosticSource::Visual);
}

TEST(UiaRegionQueryWorkerTest, ValidatesGenerationProcessDeadlineAndBounds) {
  window_detail::UiaRegionQueryResult result;
  result.request_id = 7;
  result.generation = 10;
  result.process_id = 42;
  result.deadline_ms = 1100;
  result.requested_at_ms = 1000;
  result.root_window = reinterpret_cast<HWND>(1);
  result.owner_rect = {0, 0, 800, 600};
  result.candidate_count = 1;
  result.candidates[0] = {1, 2, {100, 100, 200, 160}, SmartRegionKind::KnownContent};
  result.candidates[0].source = SmartRegionDiagnosticSource::Uia;
  result.candidates[0].semantic = SmartRegionSemantic::ActionableControl;
  window_detail::UiaRegionQueryRequest request{7, result.root_window,
      {120, 120}, result.owner_rect, 1000};
  request.generation = 10;
  request.process_id = 42;
  EXPECT_TRUE(window_detail::isUiaQueryResultApplicable(result, request, 1050));
  request.generation = 11;
  EXPECT_FALSE(window_detail::isUiaQueryResultApplicable(result, request, 1050));
  request.generation = 10;
  request.process_id = 43;
  EXPECT_FALSE(window_detail::isUiaQueryResultApplicable(result, request, 1050));
  request.process_id = 42;
  EXPECT_FALSE(window_detail::isUiaQueryResultApplicable(result, request, 1100));
  request.owner_rect.right = 700;
  EXPECT_FALSE(window_detail::isUiaQueryResultApplicable(result, request, 1050));
}

TEST(UiaRegionQueryWorkerTest, ExpiredQueuedRequestNeverEntersProvider) {
  CountingUiaQueryContext context;
  window_detail::UiaRegionQueryWorker worker(&runCountingUiaQuery, &context);
  ASSERT_TRUE(worker.start());
  window_detail::UiaRegionQueryRequest request{1, reinterpret_cast<HWND>(1),
      {100, 100}, {0, 0, 1000, 800}, GetTickCount64()};
  request.deadline_ms = GetTickCount64() - 1;
  ASSERT_TRUE(worker.request(request));
  window_detail::UiaRegionQueryResult result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, result));
  EXPECT_TRUE(result.timed_out);
  EXPECT_EQ(context.call_count, 0);
}

TEST(UiaRegionQueryWorkerTest, SlowProviderCooldownCoversWholeProcessWindow) {
  CountingUiaQueryContext context;
  auto query = [](const window_detail::UiaRegionQueryRequest& request,
      window_detail::UiaRegionQueryResult& result, void* context) noexcept {
    Sleep(100);
    runCountingUiaQuery(request, result, context);
  };
  window_detail::UiaRegionQueryWorker worker(query, &context);
  ASSERT_TRUE(worker.start());
  window_detail::UiaRegionQueryRequest request{1, reinterpret_cast<HWND>(1),
      {100, 100}, {0, 0, 1000, 800}, GetTickCount64()};
  request.process_id = 42;
  request.deadline_ms = request.requested_at_ms + 20;
  ASSERT_TRUE(worker.request(request));
  window_detail::UiaRegionQueryResult result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, result));
  EXPECT_TRUE(result.timed_out);
  request.request_id = 2;
  request.screen_point = {400, 400};
  request.requested_at_ms = GetTickCount64();
  request.deadline_ms = request.requested_at_ms + 500;
  ASSERT_TRUE(worker.request(request));
  ASSERT_TRUE(waitForUiaQueryResult(worker, result));
  EXPECT_TRUE(result.suppressed_by_cooldown);
  EXPECT_EQ(context.call_count, 1);
  request.request_id = 3;
  request.process_id = 43;
  ASSERT_TRUE(worker.request(request));
  ASSERT_TRUE(waitForUiaQueryResult(worker, result));
  EXPECT_FALSE(result.suppressed_by_cooldown);
  EXPECT_EQ(context.call_count, 2);
}

TEST(UiaRegionQueryWorkerTest,
     RejectsPreviousRequestEvenWhenCandidateContainsTheNewPointer)
{
  window_detail::UiaRegionQueryResult result;
  result.request_id = 7;
  result.root_window = reinterpret_cast<HWND>(1);
  result.screen_point = {120, 118};
  result.owner_rect = {0, 0, 800, 600};
  result.requested_at_ms = 1000;
  result.candidate_count = 1;
  result.candidates[0] = {1, 2, {100, 100, 160, 140},
                          SmartRegionKind::KnownContent};
  result.candidates[0].source = SmartRegionDiagnosticSource::Uia;
  result.candidates[0].semantic = SmartRegionSemantic::ActionableControl;
  const window_detail::UiaRegionQueryRequest current_request{
      8, reinterpret_cast<HWND>(1), {132, 122}, {0, 0, 800, 600}, 1040};

  EXPECT_FALSE(window_detail::isUiaQueryResultApplicable(result,
                                                         current_request,
                                                         1050));
}

TEST(UiaRegionQueryWorkerTest,
     RejectsLateResultForAnotherWindowOrAnotherControl)
{
  window_detail::UiaRegionQueryResult result;
  result.request_id = 7;
  result.root_window = reinterpret_cast<HWND>(1);
  result.screen_point = {120, 118};
  result.owner_rect = {0, 0, 800, 600};
  result.requested_at_ms = 1000;
  result.candidate_count = 1;
  result.candidates[0] = {1, 2, {100, 100, 160, 140},
                          SmartRegionKind::KnownContent};
  result.candidates[0].source = SmartRegionDiagnosticSource::Uia;
  result.candidates[0].semantic = SmartRegionSemantic::ActionableControl;
  const window_detail::UiaRegionQueryRequest other_window{
      8, reinterpret_cast<HWND>(3), {132, 122}, {0, 0, 800, 600}, 1040};
  const window_detail::UiaRegionQueryRequest other_control{
      8, reinterpret_cast<HWND>(1), {220, 122}, {0, 0, 800, 600}, 1040};

  EXPECT_FALSE(window_detail::isUiaQueryResultApplicable(result,
                                                          other_window,
                                                          1050));
  EXPECT_FALSE(window_detail::isUiaQueryResultApplicable(result,
                                                          other_control,
                                                          1050));
}

TEST(UiaRegionQueryWorkerTest, RejectsLateResultAfterOwnerBoundsChange)
{
  window_detail::UiaRegionQueryResult result;
  result.request_id = 7;
  result.root_window = reinterpret_cast<HWND>(1);
  result.screen_point = {120, 118};
  result.owner_rect = {0, 0, 800, 600};
  result.requested_at_ms = 1000;
  result.candidate_count = 1;
  result.candidates[0] = {1, 2, {100, 100, 160, 140},
                          SmartRegionKind::KnownContent};
  result.candidates[0].source = SmartRegionDiagnosticSource::Uia;
  result.candidates[0].semantic = SmartRegionSemantic::ActionableControl;
  const window_detail::UiaRegionQueryRequest resized_owner{
      8, reinterpret_cast<HWND>(1), {132, 122}, {0, 0, 760, 560}, 1040};

  EXPECT_FALSE(window_detail::isUiaQueryResultApplicable(result,
                                                          resized_owner,
                                                          1050));
}

TEST(BrowserChromeRegionTest, LimitsTabAndBookmarkFallbackToTopOuterShell)
{
  const WindowRect root_client_rect{0, 0, 1200, 900};
  const WindowRect renderer_rect{0, 120, 1200, 900};
  WindowRect chrome_rect;

  EXPECT_TRUE(window_detail::chromiumBrowserChromeRect(
      root_client_rect, renderer_rect, {240, 24}, chrome_rect));
  EXPECT_EQ(chrome_rect.left, 0);
  EXPECT_EQ(chrome_rect.top, 0);
  EXPECT_EQ(chrome_rect.right, 1200);
  EXPECT_EQ(chrome_rect.bottom, 120);
  EXPECT_TRUE(window_detail::chromiumBrowserChromeRect(
      root_client_rect, renderer_rect, {480, 88}, chrome_rect));
  EXPECT_FALSE(window_detail::chromiumBrowserChromeRect(
      root_client_rect, renderer_rect, {480, 240}, chrome_rect));
}

TEST(BrowserChromeRegionTest, RejectsRendererThatDoesNotShareTheRootWidth)
{
  WindowRect chrome_rect;

  EXPECT_FALSE(window_detail::chromiumBrowserChromeRect(
      {0, 0, 1200, 900}, {80, 120, 1120, 900}, {240, 24}, chrome_rect));
  EXPECT_TRUE(chrome_rect.empty());
}

TEST(BrowserChromeRegionTest, RejectsOversizedTopShellFallback)
{
  WindowRect chrome_rect;

  EXPECT_FALSE(window_detail::chromiumBrowserChromeRect(
      {0, 0, 1200, 900}, {0, 360, 1200, 900}, {240, 24}, chrome_rect));
  EXPECT_TRUE(chrome_rect.empty());
}

TEST(UiaRegionLocatorTest, MapsActionableListItemToUiaCandidate)
{
  const window_detail::UiaRegionProperties properties{
      {100, 200, 500, 240}, window_detail::UiaControlType::ListItem, true,
      true};
  SmartRegionCandidate candidate;

  ASSERT_TRUE(window_detail::makeUiaCandidate(
      reinterpret_cast<HWND>(1), {180, 220}, properties, candidate));
  EXPECT_EQ(candidate.kind, SmartRegionKind::KnownContent);
  EXPECT_EQ(candidate.source, SmartRegionDiagnosticSource::Uia);
  EXPECT_EQ(candidate.semantic, SmartRegionSemantic::ActionableControl);
  EXPECT_EQ(candidate.rect.left, 100);
  EXPECT_EQ(candidate.rect.bottom, 240);
}

TEST(UiaRegionLocatorTest, MapsImageElementToContentSurfaceCandidate)
{
  const window_detail::UiaRegionProperties properties{
      {100, 200, 500, 440}, window_detail::UiaControlType::Image, true, true};
  SmartRegionCandidate candidate;

  ASSERT_TRUE(window_detail::makeUiaCandidate(
      reinterpret_cast<HWND>(1), {180, 220}, properties, candidate));
  EXPECT_EQ(candidate.source, SmartRegionDiagnosticSource::Uia);
  EXPECT_EQ(candidate.semantic, SmartRegionSemantic::ContentSurface);
  EXPECT_EQ(candidate.rect.left, 100);
  EXPECT_EQ(candidate.rect.bottom, 440);
}

TEST(UiaRegionLocatorTest, MapsRadioButtonToActionableCandidate)
{
  const window_detail::UiaRegionProperties properties{
      {100, 200, 260, 240}, window_detail::UiaControlType::RadioButton, true,
      true, true, true, true};
  SmartRegionCandidate candidate;

  ASSERT_TRUE(window_detail::makeUiaCandidate(
      reinterpret_cast<HWND>(1), {180, 220}, properties, candidate));
  EXPECT_EQ(candidate.semantic, SmartRegionSemantic::ActionableControl);
}

TEST(UiaRegionLocatorTest, RejectsDisabledStandardControl)
{
  const window_detail::UiaRegionProperties properties{
      {100, 200, 260, 240}, window_detail::UiaControlType::Button, true,
      true, false, false, true};
  SmartRegionCandidate candidate;

  EXPECT_FALSE(window_detail::makeUiaCandidate(
      reinterpret_cast<HWND>(1), {180, 220}, properties, candidate));
}

TEST(UiaRegionLocatorTest, PreservesControlQualityMetadataOnCandidate)
{
  window_detail::UiaRegionProperties properties{
      {100, 200, 260, 240}, window_detail::UiaControlType::TabItem, true,
      true, true, true, true};
  properties.control_type_id = UIA_TabItemControlTypeId;
  properties.supported_pattern_flags =
      static_cast<std::uint8_t>(window_detail::UiaPatternFlag::SelectionItem);
  SmartRegionCandidate candidate;

  ASSERT_TRUE(window_detail::makeUiaCandidate(
      reinterpret_cast<HWND>(1), {180, 220}, properties, candidate));
  EXPECT_TRUE(candidate.uia_metadata.available);
  EXPECT_EQ(candidate.uia_metadata.control_type_id,
            static_cast<std::uint32_t>(UIA_TabItemControlTypeId));
  EXPECT_EQ(candidate.uia_metadata.pattern_flags,
            properties.supported_pattern_flags);
  EXPECT_TRUE(candidate.uia_metadata.is_control_element);
  EXPECT_TRUE(candidate.uia_metadata.is_content_element);
  EXPECT_TRUE(candidate.uia_metadata.is_enabled);
  EXPECT_TRUE(candidate.uia_metadata.is_keyboard_focusable);
  EXPECT_TRUE(candidate.uia_metadata.has_name);
  EXPECT_EQ(candidate.uia_metadata.quality,
            SmartRegionUiaQuality::NamedActionable);
}

TEST(UiaRegionLocatorTest, MapsNamedFocusableCustomControlToActionableCandidate)
{
  const window_detail::UiaRegionProperties properties{
      {100, 200, 300, 250}, window_detail::UiaControlType::Custom, true,
      true, true, true, true};
  SmartRegionCandidate candidate;

  ASSERT_TRUE(window_detail::makeUiaCandidate(
      reinterpret_cast<HWND>(1), {180, 220}, properties, candidate));
  EXPECT_EQ(candidate.semantic, SmartRegionSemantic::ActionableControl);
}

TEST(UiaRegionLocatorTest,
     MapsCompactUnnamedControlOnlyWhenItIsNotContent)
{
  const window_detail::UiaRegionProperties activity_bar_icon{
      {100, 200, 112, 218}, window_detail::UiaControlType::Custom, true,
      false, true, false, false};
  const window_detail::UiaRegionProperties unnamed_content{
      {100, 200, 112, 218}, window_detail::UiaControlType::Custom, true,
      true, true, false, false};
  SmartRegionCandidate candidate;

  ASSERT_TRUE(window_detail::makeUiaCandidate(
      reinterpret_cast<HWND>(1), {106, 208}, activity_bar_icon, candidate));
  EXPECT_EQ(candidate.semantic, SmartRegionSemantic::ActionableControl);
  EXPECT_FALSE(window_detail::makeUiaCandidate(
      reinterpret_cast<HWND>(1), {106, 208}, unnamed_content, candidate));
}

TEST(SmartRegionCandidateSelectorTest,
     AcceptsCompactUiaActionableControlWithoutRelaxingVisualMinimumSize)
{
  SmartRegionCandidate candidate{
      1, 2, {100, 200, 112, 218}, SmartRegionKind::KnownContent};
  candidate.source = SmartRegionDiagnosticSource::Uia;
  candidate.semantic = SmartRegionSemantic::ActionableControl;
  SmartRegionCandidate selected;

  ASSERT_TRUE(SmartRegionCandidateSelector::selectBest(
      &candidate, 1, 106, 208, {0, 0, 800, 600}, selected));
  EXPECT_EQ(selected.target_window, 2U);
}

TEST(UiaRegionLocatorTest,
     KeepsCompactBrowserChromeControlsAsSemanticFixtureCandidates)
{
  const window_detail::UiaRegionProperties tab_item{
      {100, 8, 260, 40}, window_detail::UiaControlType::TabItem, true, true,
      true, true, true};
  const window_detail::UiaRegionProperties icon_button{
      {204, 14, 216, 32}, window_detail::UiaControlType::Button, true, true,
      true, true, false};
  const window_detail::UiaRegionProperties bookmark_link{
      {320, 52, 440, 76}, window_detail::UiaControlType::Hyperlink, true,
      true, true, false, true};
  const window_detail::UiaRegionProperties narrow_row{
      {500, 100, 1080, 122}, window_detail::UiaControlType::ListItem, true,
      true, true, true, true};
  const window_detail::UiaRegionProperties properties[] = {
      tab_item, icon_button, bookmark_link, narrow_row};
  const POINT screen_points[] = {{180, 20}, {210, 22}, {360, 64},
                                 {640, 110}};

  for (std::size_t index = 0; index < std::size(properties); ++index)
  {
    SmartRegionCandidate candidate;
    ASSERT_TRUE(window_detail::makeUiaCandidate(
        reinterpret_cast<HWND>(1), screen_points[index], properties[index],
        candidate));
    EXPECT_EQ(candidate.source, SmartRegionDiagnosticSource::Uia);
    EXPECT_EQ(candidate.semantic, SmartRegionSemantic::ActionableControl);
  }
}

TEST(BrowserTabCandidateTest,
     KeepsCustomPatternBackedCloseButtonAndTabItemForCandidateCycling)
{
  window_detail::UiaRegionProperties close_button{
      {236, 12, 252, 28}, window_detail::UiaControlType::Custom, true, true,
      true, false, false};
  close_button.supported_pattern_flags =
      static_cast<std::uint8_t>(window_detail::UiaPatternFlag::Invoke);
  window_detail::UiaRegionProperties tab_item{
      {100, 4, 260, 40}, window_detail::UiaControlType::TabItem, true, true,
      true, false, true};
  tab_item.supported_pattern_flags =
      static_cast<std::uint8_t>(window_detail::UiaPatternFlag::SelectionItem);
  const window_detail::UiaRegionProperties tab_strip{
      {0, 0, 1000, 44}, window_detail::UiaControlType::Pane, true, true,
      true, false, true};
  const window_detail::UiaRegionProperties properties[] = {
      close_button, tab_item, tab_strip};
  SmartRegionCandidate candidates[std::size(properties)];

  const std::size_t count = window_detail::collectUiaCandidates(
      reinterpret_cast<HWND>(1), {244, 20}, properties,
      std::size(properties), candidates, std::size(candidates));

  ASSERT_EQ(count, std::size(properties));
  EXPECT_EQ(candidates[0].semantic, SmartRegionSemantic::ActionableControl);
  EXPECT_EQ(candidates[1].semantic, SmartRegionSemantic::ActionableControl);
  SmartRegionCandidate selected;
  ASSERT_TRUE(SmartRegionCandidateSelector::selectBest(
      candidates, count, 244, 20, {0, 0, 1000, 800}, selected));
  EXPECT_EQ(selected.rect.left, close_button.rect.left);

  SmartRegionCandidateCollection collection;
  collection.replace(candidates, count, 244, 20, {0, 0, 1000, 800},
                     selected);
  ASSERT_TRUE(collection.cycle(1));
  EXPECT_EQ(collection.current().rect.left, tab_item.rect.left);
}

TEST(UiaRegionLocatorTest, RejectsUnnamedOrTinyGenericContainers)
{
  const window_detail::UiaRegionProperties unnamed_group{
      {100, 200, 500, 400}, window_detail::UiaControlType::Group, true,
      true, true, false, false};
  const window_detail::UiaRegionProperties tiny_pane{
      {100, 200, 120, 216}, window_detail::UiaControlType::Pane, true,
      true, true, false, true};
  SmartRegionCandidate candidate;

  EXPECT_FALSE(window_detail::makeUiaCandidate(
      reinterpret_cast<HWND>(1), {180, 220}, unnamed_group, candidate));
  EXPECT_FALSE(window_detail::makeUiaCandidate(
      reinterpret_cast<HWND>(1), {110, 208}, tiny_pane, candidate));
}

TEST(UiaRegionLocatorTest, RejectsPatternBackedGenericPane)
{
  window_detail::UiaRegionProperties pane{
      {100, 200, 160, 240}, window_detail::UiaControlType::Pane, true, true,
      true, false, false};
  pane.supported_pattern_flags =
      static_cast<std::uint8_t>(window_detail::UiaPatternFlag::Invoke);
  SmartRegionCandidate candidate;

  EXPECT_FALSE(window_detail::makeUiaCandidate(
      reinterpret_cast<HWND>(1), {120, 220}, pane, candidate));
}

TEST(UiaRegionLocatorTest, MapsNamedContentPaneToContentSurfaceCandidate)
{
  const window_detail::UiaRegionProperties properties{
      {100, 200, 500, 400}, window_detail::UiaControlType::Pane, true,
      true, true, false, true};
  SmartRegionCandidate candidate;

  ASSERT_TRUE(window_detail::makeUiaCandidate(
      reinterpret_cast<HWND>(1), {180, 220}, properties, candidate));
  EXPECT_EQ(candidate.semantic, SmartRegionSemantic::ContentSurface);
}

TEST(UiaRegionLocatorTest, SelectsSmallestChildContainingPointer)
{
  const window_detail::UiaRegionProperties children[] = {
      {{0, 0, 1000, 800}, window_detail::UiaControlType::Pane, true, true},
      {{100, 100, 140, 136}, window_detail::UiaControlType::Button, true,
       true},
      {{500, 100, 700, 300}, window_detail::UiaControlType::Group, true,
       true},
  };
  std::size_t selected_index = 0;

  ASSERT_TRUE(window_detail::selectSmallestUiaChildAtPoint(
      children, std::size(children), {120, 118}, selected_index));
  EXPECT_EQ(selected_index, 1U);
}

TEST(UiaRegionLocatorTest, DetectsHitPathOutsideRequestedRootWindow)
{
  window_detail::UiaRegionProperties path[] = {
      {{0, 0, 1920, 1080}, window_detail::UiaControlType::Pane, true, true},
      {{0, 0, 1920, 1080}, window_detail::UiaControlType::Pane, true,
       true},
  };
  path[1].native_window = reinterpret_cast<HWND>(2);

  EXPECT_FALSE(window_detail::uiaPathBelongsToRoot(
      path, std::size(path), reinterpret_cast<HWND>(1)));
  path[1].native_window = reinterpret_cast<HWND>(1);
  EXPECT_TRUE(window_detail::uiaPathBelongsToRoot(
      path, std::size(path), reinterpret_cast<HWND>(1)));
}

TEST(UiaRegionLocatorTest, RootScopedFallbackRejectsLargeGenericControl)
{
  SmartRegionCandidate candidate{
      1, 1, {100, 100, 1700, 900}, SmartRegionKind::KnownContent};
  candidate.source = SmartRegionDiagnosticSource::Uia;
  candidate.semantic = SmartRegionSemantic::ActionableControl;

  EXPECT_FALSE(window_detail::isUsefulRootScopedUiaCandidate(
      candidate, {0, 0, 1920, 1080}));
}

TEST(UiaRegionLocatorTest, RootScopedFallbackKeepsSmallControlAndNarrowRow)
{
  SmartRegionCandidate button{
      1, 1, {100, 100, 148, 132}, SmartRegionKind::KnownContent};
  button.source = SmartRegionDiagnosticSource::Uia;
  button.semantic = SmartRegionSemantic::ActionableControl;
  SmartRegionCandidate row{
      1, 1, {300, 400, 1700, 480}, SmartRegionKind::KnownContent};
  row.source = SmartRegionDiagnosticSource::Uia;
  row.semantic = SmartRegionSemantic::ContentSurface;

  EXPECT_TRUE(window_detail::isUsefulRootScopedUiaCandidate(
      button, {0, 0, 1920, 1080}));
  EXPECT_TRUE(window_detail::isUsefulRootScopedUiaCandidate(
      row, {0, 0, 1920, 1080}));
}

TEST(UiaRegionLocatorTest,
     CollectsActionableElementAndContainingContentAncestor)
{
  const window_detail::UiaRegionProperties properties[] = {
      {{120, 240, 720, 280}, window_detail::UiaControlType::ListItem, true,
       true},
      {{100, 100, 900, 700}, window_detail::UiaControlType::List, true,
       true},
  };
  SmartRegionCandidate candidates[2];

  const std::size_t count = window_detail::collectUiaCandidates(
      reinterpret_cast<HWND>(1), {200, 260}, properties,
      std::size(properties), candidates, std::size(candidates));

  ASSERT_EQ(count, 2U);
  EXPECT_EQ(candidates[0].semantic, SmartRegionSemantic::ActionableControl);
  EXPECT_EQ(candidates[0].rect.left, 120);
  EXPECT_EQ(candidates[1].semantic, SmartRegionSemantic::ContentSurface);
  EXPECT_EQ(candidates[1].rect.left, 100);
}

TEST(UiaRegionLocatorTest, RejectsUnknownOrOutOfBoundsElement)
{
  const window_detail::UiaRegionProperties unknown{
      {100, 200, 500, 240}, window_detail::UiaControlType::Unknown, true,
      true};
  const window_detail::UiaRegionProperties list_item{
      {100, 200, 500, 240}, window_detail::UiaControlType::ListItem, true,
      true};
  SmartRegionCandidate candidate;

  EXPECT_FALSE(window_detail::makeUiaCandidate(
      reinterpret_cast<HWND>(1), {180, 220}, unknown, candidate));
  EXPECT_FALSE(window_detail::makeUiaCandidate(
      reinterpret_cast<HWND>(1), {520, 220}, list_item, candidate));
  EXPECT_FALSE(candidate.valid());
}

TEST(WindowQueryHelpersTest, AcceptsOnlyBrowserOwnedPopupStyles)
{
  EXPECT_TRUE(window_detail::isBrowserOwnedTransientPopupStyle(WS_POPUP,
                                                                true));
  EXPECT_FALSE(window_detail::isBrowserOwnedTransientPopupStyle(
      WS_OVERLAPPEDWINDOW, true));
  EXPECT_FALSE(window_detail::isBrowserOwnedTransientPopupStyle(WS_POPUP,
                                                                 false));
}

TEST(MsaaRegionLocatorTest, MapsFocusableTextToActionableCandidate)
{
  const window_detail::MsaaRegionProperties properties{
      {100, 200, 500, 240}, ROLE_SYSTEM_TEXT, STATE_SYSTEM_FOCUSABLE};
  SmartRegionCandidate candidate;

  ASSERT_TRUE(window_detail::makeMsaaCandidate(
      reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(2), {180, 220},
      properties, candidate));
  EXPECT_EQ(candidate.source, SmartRegionDiagnosticSource::Msaa);
  EXPECT_EQ(candidate.semantic, SmartRegionSemantic::ActionableControl);
  EXPECT_EQ(candidate.target_window, 2U);
}

TEST(MsaaRegionLocatorTest,
     MapsBrowserChromeControlsAndToolbarFallbackToLocalCandidates)
{
  const window_detail::MsaaRegionProperties bookmark_link{
      {120, 80, 260, 112}, ROLE_SYSTEM_LINK, 0};
  const window_detail::MsaaRegionProperties address_text{
      {300, 72, 1400, 120}, ROLE_SYSTEM_TEXT, STATE_SYSTEM_FOCUSABLE};
  const window_detail::MsaaRegionProperties reload_button{
      {252, 72, 284, 120}, ROLE_SYSTEM_PUSHBUTTON, 0};
  const window_detail::MsaaRegionProperties menu_button{
      {2069, 69, 2121, 120}, ROLE_SYSTEM_BUTTONMENU, STATE_SYSTEM_FOCUSABLE};
  const window_detail::MsaaRegionProperties toolbar{
      {0, 64, 1920, 128}, ROLE_SYSTEM_TOOLBAR, 0};
  const window_detail::MsaaRegionProperties properties[] = {
      bookmark_link, address_text, reload_button, menu_button, toolbar};
  const POINT points[] = {
      {180, 96}, {640, 96}, {268, 96}, {2095, 94}, {1800, 96}};
  const SmartRegionSemantic expected_semantics[] = {
      SmartRegionSemantic::ActionableControl,
      SmartRegionSemantic::ActionableControl,
      SmartRegionSemantic::ActionableControl,
      SmartRegionSemantic::ActionableControl,
      SmartRegionSemantic::ContentSurface};

  for (std::size_t index = 0; index < std::size(properties); ++index)
  {
    SmartRegionCandidate candidate;
    ASSERT_TRUE(window_detail::makeMsaaCandidate(
        reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(1), points[index],
        properties[index], candidate));
    EXPECT_EQ(candidate.semantic, expected_semantics[index]);
    EXPECT_EQ(candidate.rect.left, properties[index].rect.left);
    EXPECT_EQ(candidate.rect.top, properties[index].rect.top);
    EXPECT_EQ(candidate.rect.right, properties[index].rect.right);
    EXPECT_EQ(candidate.rect.bottom, properties[index].rect.bottom);
  }
}

TEST(MsaaRegionLocatorTest, PreservesRoleAndBoundedHitTestDepth)
{
  const window_detail::MsaaRegionProperties properties{
      {100, 200, 260, 240}, ROLE_SYSTEM_PAGETAB, 0};
  SmartRegionCandidate candidate;

  ASSERT_TRUE(window_detail::makeMsaaCandidate(
      reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(2), {180, 220},
      properties, candidate, 4));
  EXPECT_EQ(candidate.accessibility_role,
            static_cast<std::uint32_t>(ROLE_SYSTEM_PAGETAB));
  EXPECT_EQ(candidate.accessibility_depth, 4U);
}

TEST(MsaaRegionLocatorTest, RecordsUnknownSemanticFilteredNode)
{
  const window_detail::MsaaRegionProperties properties{
      {100, 200, 260, 240}, ROLE_SYSTEM_SEPARATOR, STATE_SYSTEM_FOCUSABLE};
  SmartRegionCandidate candidate;
  SmartRegionMsaaFilteredNodeDiagnostic filtered_node;

  EXPECT_FALSE(window_detail::makeMsaaCandidate(
      reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(1), {180, 220},
      properties, candidate, 4, &filtered_node));
  EXPECT_FALSE(candidate.valid());
  EXPECT_EQ(filtered_node.reason,
            SmartRegionMsaaFilteredNodeReason::UnknownSemantic);
  EXPECT_EQ(filtered_node.role, ROLE_SYSTEM_SEPARATOR);
  EXPECT_EQ(filtered_node.state, STATE_SYSTEM_FOCUSABLE);
  EXPECT_EQ(filtered_node.rect.left, 100);
  EXPECT_EQ(filtered_node.rect.top, 200);
  EXPECT_EQ(filtered_node.rect.right, 260);
  EXPECT_EQ(filtered_node.rect.bottom, 240);
  EXPECT_EQ(filtered_node.accessibility_depth, 4U);
}

TEST(MsaaRegionLocatorTest,
     RecoversOnlyBrowserTopChromeSemanticAndVisibilityMisses)
{
  EXPECT_TRUE(window_detail::msaaShouldRecoverBrowserFilteredNode(
      true, true, SmartRegionMsaaFilteredNodeReason::UnknownSemantic));
  EXPECT_TRUE(window_detail::msaaShouldRecoverBrowserFilteredNode(
      true, true,
      SmartRegionMsaaFilteredNodeReason::InvisibleOrOffscreen));
  EXPECT_FALSE(window_detail::msaaShouldRecoverBrowserFilteredNode(
      false, true, SmartRegionMsaaFilteredNodeReason::UnknownSemantic));
  EXPECT_FALSE(window_detail::msaaShouldRecoverBrowserFilteredNode(
      true, false, SmartRegionMsaaFilteredNodeReason::UnknownSemantic));
  EXPECT_FALSE(window_detail::msaaShouldRecoverBrowserFilteredNode(
      true, true, SmartRegionMsaaFilteredNodeReason::PointerOutside));
}

TEST(MsaaRegionLocatorTest, UsesDeeperHitTestTraversalOnlyForBrowsers)
{
  EXPECT_EQ(window_detail::msaaHitTestDepthLimit(false), 6U);
  EXPECT_EQ(window_detail::msaaHitTestDepthLimit(true), 10U);
}

TEST(MsaaRegionLocatorTest,
     EnumeratesOnlyBrowserToolbarAndPaneContainerChildren)
{
  const window_detail::MsaaRegionProperties toolbar{
      {0, 60, 2560, 120}, ROLE_SYSTEM_TOOLBAR, 0};
  const window_detail::MsaaRegionProperties pane{
      {1091, 0, 1249, 62}, ROLE_SYSTEM_PANE, 0};
  const window_detail::MsaaRegionProperties button{
      {2022, 0, 2064, 62}, ROLE_SYSTEM_PUSHBUTTON, 0};
  const window_detail::MsaaRegionProperties document{
      {0, 180, 2560, 1528}, ROLE_SYSTEM_DOCUMENT, 0};

  EXPECT_TRUE(window_detail::msaaShouldEnumerateChildren(toolbar));
  EXPECT_TRUE(window_detail::msaaShouldEnumerateChildren(pane));
  EXPECT_FALSE(window_detail::msaaShouldEnumerateChildren(button));
  EXPECT_FALSE(window_detail::msaaShouldEnumerateChildren(document));
}

TEST(MsaaRegionLocatorTest,
     DefersOnlyDirectBrowserContainerCandidatesForChildTraversal)
{
  const window_detail::MsaaRegionProperties toolbar{
      {0, 60, 2560, 120}, ROLE_SYSTEM_TOOLBAR, 0};
  const window_detail::MsaaRegionProperties pane{
      {0, 60, 2560, 120}, ROLE_SYSTEM_PANE, 0};
  const window_detail::MsaaRegionProperties button{
      {2022, 0, 2064, 62}, ROLE_SYSTEM_PUSHBUTTON, 0};

  EXPECT_TRUE(window_detail::msaaShouldDeferDirectBrowserContainerCandidate(
      true, toolbar));
  EXPECT_TRUE(window_detail::msaaShouldDeferDirectBrowserContainerCandidate(
      true, pane));
  EXPECT_FALSE(window_detail::msaaShouldDeferDirectBrowserContainerCandidate(
      true, button));
  EXPECT_FALSE(window_detail::msaaShouldDeferDirectBrowserContainerCandidate(
      false, toolbar));
}

TEST(MsaaRegionLocatorTest,
     RecursesOnlyIntoBrowserChildrenThatContainTheCursor)
{
  const WindowRect bookmark_folder{320, 120, 460, 152};
  const WindowRect earlier_bookmark{120, 120, 280, 152};
  const POINT cursor{390, 136};

  EXPECT_TRUE(
      window_detail::msaaChildContainsScreenPoint(bookmark_folder, cursor));
  EXPECT_FALSE(
      window_detail::msaaChildContainsScreenPoint(earlier_bookmark, cursor));
}

TEST(MsaaRegionLocatorTest,
     RequestsBoundedAccessibleChildrenWhenChromiumReportsNoChildren)
{
  EXPECT_EQ(window_detail::msaaAccessibleChildrenRequestCount(12, 96), 12);
  EXPECT_EQ(window_detail::msaaAccessibleChildrenRequestCount(48, 96), 32);
  EXPECT_EQ(window_detail::msaaAccessibleChildrenRequestCount(0, 96), 32);
  EXPECT_EQ(window_detail::msaaAccessibleChildrenRequestCount(48, 7), 7);
  EXPECT_EQ(window_detail::msaaAccessibleChildrenRequestCount(0, 0), 0);
}

TEST(MsaaRegionLocatorTest, RejectsWindowSizedContentSurfaceAtSource)
{
  TestWindowTree root_window(L"QingYingMsaaContentRootWindow",
                             L"QingYingMsaaContentRootChild", 40, 80,
                             400, 240, 0);
  ASSERT_NE(root_window.root(), nullptr);
  RECT root_rect{};
  ASSERT_NE(GetWindowRect(root_window.root(), &root_rect), FALSE);
  const POINT point{root_rect.left + 20, root_rect.top + 20};
  const window_detail::MsaaRegionProperties properties{
      {root_rect.left, root_rect.top, root_rect.right, root_rect.bottom},
      ROLE_SYSTEM_PANE, 0};
  SmartRegionCandidate candidate;

  EXPECT_FALSE(window_detail::makeMsaaCandidate(
      root_window.root(), root_window.root(), point, properties, candidate));
  EXPECT_FALSE(candidate.valid());
}

TEST(MsaaRegionLocatorTest, TreatsMaximizedClientSurfaceAsWindowSized)
{
  const WindowRect root_client_rect{0, 0, 2560, 1528};
  const WindowRect msaa_content_surface{0, 0, 2561, 1529};

  EXPECT_TRUE(window_detail::msaaRectCoversRootClientArea(
      msaa_content_surface, root_client_rect));
}

TEST(MsaaRegionLocatorTest, RejectsInvisibleOrPointerOutsideObject)
{
  const window_detail::MsaaRegionProperties invisible{
      {100, 200, 500, 240}, ROLE_SYSTEM_PUSHBUTTON, STATE_SYSTEM_INVISIBLE};
  const window_detail::MsaaRegionProperties visible{
      {100, 200, 500, 240}, ROLE_SYSTEM_PUSHBUTTON, 0};
  SmartRegionCandidate candidate;

  EXPECT_FALSE(window_detail::makeMsaaCandidate(
      reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(2), {180, 220},
      invisible, candidate));
  EXPECT_FALSE(window_detail::makeMsaaCandidate(
      reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(2), {520, 220},
      visible, candidate));
}

TEST(MsaaRegionLocatorTest, RejectsCandidateFromForeignTopLevelWindow)
{
  TestWindowTree root_window(L"QingYingMsaaRootWindow",
                             L"QingYingMsaaRootChild", 40, 80, 400, 240,
                             0);
  TestWindowTree foreign_window(L"QingYingMsaaForeignWindow",
                                L"QingYingMsaaForeignChild", 40, 80, 400,
                                240, 0);
  ASSERT_NE(root_window.root(), nullptr);
  ASSERT_NE(root_window.content(), nullptr);
  ASSERT_NE(foreign_window.root(), nullptr);
  const RECT content_rect = root_window.contentRect();
  const POINT point{content_rect.left + 20, content_rect.top + 20};
  const window_detail::MsaaRegionProperties properties{
      {content_rect.left, content_rect.top, content_rect.right,
       content_rect.bottom},
      ROLE_SYSTEM_PUSHBUTTON, 0};
  SmartRegionCandidate candidate;

  EXPECT_FALSE(window_detail::makeMsaaCandidate(
      root_window.root(), foreign_window.root(), point, properties,
      candidate));
}


struct LatencyQueryContext {
  std::mutex mutex;
  std::condition_variable condition;
  int entered{0};
  int released{0};
  void release(int count) {
    { std::lock_guard<std::mutex> lock(mutex); released = count; }
    condition.notify_all();
  }
  bool waitEntered(int count) {
    std::unique_lock<std::mutex> lock(mutex);
    return condition.wait_for(lock, std::chrono::seconds(1),
                              [&] { return entered >= count; });
  }
};

void runLatencyQuery(const window_detail::UiaRegionQueryRequest& request,
                     window_detail::UiaRegionQueryResult& result,
                     void* opaque) noexcept {
  auto& context = *static_cast<LatencyQueryContext*>(opaque);
  {
    std::unique_lock<std::mutex> lock(context.mutex);
    const int call = ++context.entered;
    context.condition.notify_all();
    context.condition.wait(lock, [&] { return context.released >= call; });
  }
  result.succeeded = true;
  result.candidate_count = 1;
  result.candidates[0] = {reinterpret_cast<std::uintptr_t>(request.root_window),
      request.request_id, {80, 80, 180, 180}, SmartRegionKind::KnownContent};
  result.candidates[0].source = SmartRegionDiagnosticSource::Uia;
  result.candidates[0].semantic = SmartRegionSemantic::ActionableControl;
}

struct ReleaseLatencyQuery {
  LatencyQueryContext& context;
  ~ReleaseLatencyQuery() { context.release(10000); }
};

TEST(UiaRegionQueryWorkerTest, ContinuousMotionReceivesCompletedLocalHitBeforeNextProviderReturns) {
  LatencyQueryContext context;
  window_detail::UiaRegionQueryWorker worker(&runLatencyQuery, &context);
  ReleaseLatencyQuery release{context};
  ASSERT_TRUE(worker.start());
  const auto now = GetTickCount64();
  window_detail::UiaRegionQueryRequest request{1, reinterpret_cast<HWND>(1),
      {100, 100}, {0, 0, 1000, 800}, now};
  request.generation = 7;
  request.process_id = 9;
  request.deadline_ms = now + 120;
  ASSERT_TRUE(worker.request(request));
  ASSERT_TRUE(context.waitEntered(1));
  // No idle interval: all newer samples arrive while the first query is active.
  for (int i = 2; i <= 8; ++i) {
    request.request_id = i;
    request.screen_point = {100 + i, 100 + i};
    ASSERT_TRUE(worker.request(request));
  }
  context.release(1);
  window_detail::UiaRegionQueryResult result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, result));
  EXPECT_EQ(result.request_id, 8U);
  EXPECT_EQ(result.candidates[0].target_window, 1U);
  EXPECT_TRUE(window_detail::isUiaQueryResultApplicable(result, request, GetTickCount64()));
}

TEST(UiaRegionQueryWorkerTest, DiscoveryCompletesWhileAccessibilityProviderIsBlocked) {
  LatencyQueryContext accessibility;
  CountingUiaQueryContext discovery;
  window_detail::UiaRegionQueryWorker access_worker(&runLatencyQuery, &accessibility,
      window_detail::RegionQueryLane::Accessibility);
  ReleaseLatencyQuery release{accessibility};
  window_detail::UiaRegionQueryWorker discovery_worker(&runCountingUiaQuery, &discovery,
      window_detail::RegionQueryLane::Discovery);
  ASSERT_TRUE(access_worker.start());
  ASSERT_TRUE(discovery_worker.start());
  window_detail::UiaRegionQueryRequest request{1, reinterpret_cast<HWND>(1),
      {100, 100}, {0, 0, 1000, 800}, GetTickCount64()};
  ASSERT_TRUE(access_worker.request(request));
  ASSERT_TRUE(accessibility.waitEntered(1));
  ASSERT_TRUE(discovery_worker.request(request));
  window_detail::UiaRegionQueryResult result;
  EXPECT_TRUE(waitForUiaQueryResult(discovery_worker, result));
  EXPECT_TRUE(result.succeeded);
  EXPECT_EQ(discovery.call_count, 1);
  EXPECT_TRUE(access_worker.hasPendingWork());
  EXPECT_NE(discovery_worker.diagnosticSnapshot().find("region_discovery_worker"),
            std::string::npos);
}

TEST(UiaRegionQueryWorkerTest, RebindingRequiresSameContextFreshnessAndLocalGeometry) {
  window_detail::UiaRegionQueryRequest request{2, reinterpret_cast<HWND>(1),
      {100, 100}, {0, 0, 1000, 800}, 1020};
  request.generation = 3;
  request.process_id = 4;
  window_detail::UiaRegionQueryResult result;
  result.request_id = 1;
  result.root_window = request.root_window;
  result.owner_rect = request.owner_rect;
  result.generation = 3;
  result.process_id = 4;
  result.requested_at_ms = 1000;
  result.deadline_ms = 1120;
  result.candidate_count = 1;
  result.candidates[0] = {1, 7, {80, 80, 180, 180}, SmartRegionKind::KnownContent};
  result.candidates[0].source = SmartRegionDiagnosticSource::Uia;
  result.candidates[0].semantic = SmartRegionSemantic::ActionableControl;
  const auto original = result;
  EXPECT_TRUE(window_detail::rebindUiaQueryResult(result, request, 1050));
  EXPECT_EQ(result.request_id, 2U);
  EXPECT_EQ(result.requested_at_ms, 1000U);
  EXPECT_EQ(result.deadline_ms, 1120U);
  result = original;
  request.generation++;
  EXPECT_FALSE(window_detail::rebindUiaQueryResult(result, request, 1050));
  request.generation--;
  request.process_id++;
  EXPECT_FALSE(window_detail::rebindUiaQueryResult(result, request, 1050));
  request.process_id--;
  request.screen_point = {300, 300};
  EXPECT_FALSE(window_detail::rebindUiaQueryResult(result, request, 1050));
  request.screen_point = {100, 100};
  EXPECT_FALSE(window_detail::rebindUiaQueryResult(result, request, 1120));
  result.candidates[0].kind = SmartRegionKind::Window;
  result.candidates[0].source = SmartRegionDiagnosticSource::Window;
  result.candidates[0].semantic = SmartRegionSemantic::Fallback;
  EXPECT_FALSE(window_detail::rebindUiaQueryResult(result, request, 1050));
}

TEST(UiaRegionQueryWorkerTest, FrozenSnapshotCacheIsReusedOnlyForSameImage) {
  CountingUiaQueryContext context;
  window_detail::UiaRegionQueryWorker worker(&runCountingUiaQuery, &context);
  ASSERT_TRUE(worker.start());
  window_detail::UiaRegionQueryRequest request{1, reinterpret_cast<HWND>(1),
      {100, 100}, {0, 0, 1000, 800}, GetTickCount64()};
  request.background = std::make_shared<Image>();
  request.image_screen_rect = request.owner_rect;
  ASSERT_TRUE(worker.request(request));
  window_detail::UiaRegionQueryResult result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, result));
  request.request_id++;
  request.requested_at_ms = GetTickCount64();
  ASSERT_TRUE(worker.request(request));
  ASSERT_TRUE(waitForUiaQueryResult(worker, result));
  EXPECT_TRUE(result.cache_hit);
  worker.beginStop();
  ASSERT_TRUE(worker.joinUntil(std::chrono::steady_clock::now() + std::chrono::seconds(1)));
  EXPECT_EQ(context.call_count, 1);

  // A separate worker proves an image change cannot reuse either the cache or latest slot.
  CountingUiaQueryContext changed_context;
  window_detail::UiaRegionQueryWorker changed(&runCountingUiaQuery, &changed_context);
  ASSERT_TRUE(changed.start());
  request.request_id = 1;
  ASSERT_TRUE(changed.request(request));
  ASSERT_TRUE(waitForUiaQueryResult(changed, result));
  request.request_id = 2;
  request.background = std::make_shared<Image>();
  request.requested_at_ms = GetTickCount64();
  ASSERT_TRUE(changed.request(request));
  ASSERT_TRUE(waitForUiaQueryResult(changed, result));
  EXPECT_FALSE(result.cache_hit);
  EXPECT_EQ(changed_context.call_count, 2);
}

TEST(UiaRegionQueryWorkerTest, CompletionPostsUiNotificationWithoutPollingTimer) {
  const HWND target = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
      HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
  ASSERT_NE(target, nullptr);
  CountingUiaQueryContext context;
  window_detail::UiaRegionQueryWorker worker(&runCountingUiaQuery, &context);
  ASSERT_TRUE(worker.start());
  window_detail::UiaRegionQueryRequest request{1, reinterpret_cast<HWND>(1),
      {100, 100}, {0, 0, 1000, 800}, GetTickCount64()};
  request.notify_window = target;
  EXPECT_TRUE(worker.request(request));
  bool notified = false;
  for (int i = 0; i < 50 && !notified; ++i) {
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_POSTMESSAGE);
    MSG message{};
    notified = PeekMessageW(&message, target, WM_QINGYING_SMART_REGION_COMPLETE,
        WM_QINGYING_SMART_REGION_COMPLETE, PM_REMOVE) != FALSE;
  }
  EXPECT_TRUE(notified);
  window_detail::UiaRegionQueryResult result;
  EXPECT_TRUE(worker.tryTakeLatest(result));
  worker.stop();
  DestroyWindow(target);
}

TEST(SmartRegionHoverStabilizerTest, WindowSnapshotsPreserveLocalCandidateAndSwitchStartTime) {
  SmartRegionCandidate first{1, 11, {80, 80, 180, 180}, SmartRegionKind::KnownContent};
  first.source = SmartRegionDiagnosticSource::Visual;
  first.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate next = first;
  next.target_window = 12;
  next.rect = {90, 90, 190, 190};
  SmartRegionCandidate fallback{1, 1, {0, 0, 1000, 800}, SmartRegionKind::Window};
  fallback.source = SmartRegionDiagnosticSource::Window;
  fallback.semantic = SmartRegionSemantic::Fallback;
  SmartRegionHoverStabilizer stabilizer;
  ASSERT_TRUE(stabilizer.update(first, 100, {100, 100}));
  EXPECT_FALSE(stabilizer.update(fallback, 116, {100, 100}));
  EXPECT_FALSE(stabilizer.hasPendingCandidate());
  EXPECT_EQ(stabilizer.stableCandidate().target_window, 11U);
  EXPECT_FALSE(stabilizer.update(next, 120, {100, 100}));
  for (std::uint64_t time : {136U, 152U}) {
    EXPECT_FALSE(stabilizer.update(fallback, time, {100, 100}));
    EXPECT_EQ(stabilizer.pendingSinceMs(), 120U);
  }
  EXPECT_TRUE(stabilizer.update(fallback, 168, {100, 100}));
  EXPECT_EQ(stabilizer.stableCandidate().target_window, 12U);
  EXPECT_FALSE(stabilizer.hasPendingCandidate());
}

TEST(SmartRegionHoverStabilizerTest, LeavingPendingLocalHitDiscardsItBeforeClickSelection) {
  SmartRegionCandidate first{1, 11, {80, 80, 180, 180}, SmartRegionKind::KnownContent};
  first.source = SmartRegionDiagnosticSource::Visual;
  first.semantic = SmartRegionSemantic::ContentSurface;
  SmartRegionCandidate next = first;
  next.target_window = 12;
  next.rect = {90, 90, 190, 190};
  SmartRegionCandidate fallback{1, 1, {0, 0, 1000, 800}, SmartRegionKind::Window};
  fallback.source = SmartRegionDiagnosticSource::Window;
  fallback.semantic = SmartRegionSemantic::Fallback;
  SmartRegionHoverStabilizer stabilizer;
  ASSERT_TRUE(stabilizer.update(first, 100, {100, 100}));
  ASSERT_FALSE(stabilizer.update(next, 120, {100, 100}));
  ASSERT_TRUE(stabilizer.hasPendingCandidate());
  EXPECT_FALSE(stabilizer.update(fallback, 136, {85, 85}));
  EXPECT_FALSE(stabilizer.hasPendingCandidate());
  EXPECT_EQ(stabilizer.selectionCandidate().target_window, 11U);
}

TEST(SmartRegionWindowSnapshotCacheTest,
     ReusesOnlyFreshSnapshotsThatContainThePointer) {
  std::uint64_t now_ms = 100;
  SmartRegionWindowSnapshotCache cache(
      [](void* context) noexcept {
        return *static_cast<std::uint64_t*>(context);
      },
      &now_ms);
  SmartRegionWindowSnapshot snapshot;
  snapshot.root_window = 123;
  snapshot.owner_rect = {100, 80, 700, 500};
  snapshot.client_rect = {110, 120, 690, 490};
  cache.store(snapshot);

  now_ms += 16;
  SmartRegionWindowSnapshot cached;
  std::uint64_t age_ms = 0;
  ASSERT_TRUE(cache.lookup(180, 160, cached, &age_ms));
  EXPECT_EQ(cached.root_window, snapshot.root_window);
  EXPECT_EQ(age_ms, 16U);
  EXPECT_FALSE(cache.lookup(701, 160, cached));

  now_ms += SmartRegionWindowSnapshotCache::lifetimeMs();
  EXPECT_FALSE(cache.lookup(180, 160, cached));
  const auto stats = cache.stats();
  EXPECT_EQ(stats.hits, 1U);
  EXPECT_EQ(stats.stores, 1U);
  EXPECT_GE(stats.misses, 2U);
  EXPECT_GE(stats.invalidations, 1U);
  EXPECT_EQ(cache.storageBytes(), sizeof(cache));
}

}  // namespace
}  // namespace qingying
