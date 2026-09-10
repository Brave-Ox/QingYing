#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include <Windows.h>
#include <oleacc.h>

#include <gtest/gtest.h>

#include "qingying/window/smart_region_detector.hpp"
#include "known_content_locator.hpp"
#include "msaa_region_locator.hpp"
#include "uia_region_query_worker.hpp"
#include "uia_region_locator.hpp"

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
  const SmartRegionDiagnosticEvent event{
      SmartRegionDiagnosticSource::KnownContent, {20, 30, 220, 130}, 7};

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

TEST(SmartRegionDiagnosticTraceTest, AppendsOverlayAndStabilizationTimings)
{
  SmartRegionDiagnosticTrace trace;
  SmartRegionDiagnosticEvent event;
  trace.setEnabled(true);
  ASSERT_TRUE(trace.record(event));

  EXPECT_TRUE(trace.recordOverlayRenderElapsed(24));
  EXPECT_TRUE(trace.recordStabilizationDelay(
      SmartRegionHoverStabilizer::CandidateSwitchDelayMs));
  EXPECT_EQ(trace.latestEvent().overlay_render_ms, 24U);
  EXPECT_EQ(trace.latestEvent().stabilization_delay_ms,
            SmartRegionHoverStabilizer::CandidateSwitchDelayMs);
  EXPECT_FALSE(trace.latestEvent().msaa_lookup_attempted);
  EXPECT_EQ(trace.latestEvent().msaa_lookup_ms, 0U);
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

  ASSERT_TRUE(worker.request({2, reinterpret_cast<HWND>(1), {140, 140},
                              {0, 0, 1000, 800}, 140}));
  window_detail::UiaRegionQueryResult cooled_result;
  ASSERT_TRUE(waitForUiaQueryResult(worker, cooled_result));
  EXPECT_EQ(cooled_result.request_id, 2U);
  EXPECT_TRUE(cooled_result.suppressed_by_cooldown);
  EXPECT_EQ(context.call_count, 1);
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
      result, fast_fallback, selected));
  EXPECT_EQ(selected.target_window, 2U);
  EXPECT_EQ(selected.source, SmartRegionDiagnosticSource::Uia);
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

}  // namespace
}  // namespace qingying
