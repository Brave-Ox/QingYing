#include <gtest/gtest.h>
#include "qingying/window/smart_region_detector.hpp"
#include "qingying/window/smart_region_query.hpp"
#include "qingying/diagnostics/fault_boundary.h"
#include "browser_shell_atlas.hpp"
#include <atomic>
#include <stdexcept>
#include <thread>

namespace qingying {
namespace {
struct FakeProviders {
  int accessibility_calls{0};
  int visual_calls{0};
  bool window_found{true};
  static bool snapshot(int, int, SmartRegionWindowSnapshot& out, void* context) noexcept {
    if (!static_cast<FakeProviders*>(context)->window_found) return false;
    out.root_window = 123;  // Deliberately not a real HWND.
    out.owner_rect = {0, 0, 800, 600};
    out.client_rect = {0, 0, 800, 600};
    return true;
  }
  static std::size_t accessibility(const SmartRegionWindowSnapshot& snapshot, POINT,
      SmartRegionCandidate* out, std::size_t capacity, void* context) noexcept {
    ++static_cast<FakeProviders*>(context)->accessibility_calls;
    if (!capacity) return 0;
    out[0].owner_window = snapshot.root_window;
    out[0].target_window = snapshot.root_window;
    out[0].rect = {20, 20, 120, 80};
    out[0].kind = SmartRegionKind::KnownContent;
    out[0].source = SmartRegionDiagnosticSource::Msaa;
    out[0].semantic = SmartRegionSemantic::ActionableControl;
    return 1;
  }
  static bool visual(const SmartRegionVisualContext&, const SmartRegionWindowSnapshot& snapshot,
      POINT, SmartRegionCandidate& out, void* context) noexcept {
    ++static_cast<FakeProviders*>(context)->visual_calls;
    out.owner_window = snapshot.root_window;
    out.target_window = snapshot.root_window;
    out.rect = {10, 10, 200, 150};
    out.kind = SmartRegionKind::KnownContent;
    out.source = SmartRegionDiagnosticSource::Visual;
    out.semantic = SmartRegionSemantic::ContentSurface;
    out.visual_confidence = 95;
    return true;
  }
  SmartRegionProviders providers() noexcept {
    return {this, snapshot, accessibility, visual};
  }
};
TEST(SmartRegionBoundaryTest, MergesFakeProvidersWithoutNativeWindow) {
  FakeProviders fake;
  SmartRegionDetector detector(fake.providers());
  SmartRegionCandidate selected;
  SmartRegionCandidateCollection collection;
  SmartRegionVisualContext visual;
  ASSERT_TRUE(detector.detectAt(50, 50, selected, nullptr, &visual,
      SmartRegionDetectionPolicy::Complete, &collection));
  EXPECT_EQ(selected.source, SmartRegionDiagnosticSource::Msaa);
  EXPECT_EQ(fake.accessibility_calls, 1);
  EXPECT_EQ(fake.visual_calls, 1);
  EXPECT_GE(collection.count(), 3u);
}
TEST(SmartRegionBoundaryTest, UiSnapshotNeverQueriesHeavyProviders) {
  FakeProviders fake;
  SmartRegionDetector detector(fake.providers());
  SmartRegionCandidate selected;
  SmartRegionVisualContext visual;
  ASSERT_TRUE(detector.detectAt(50, 50, selected, nullptr, &visual,
      SmartRegionDetectionPolicy::UiSnapshot));
  EXPECT_EQ(fake.accessibility_calls, 0);
  EXPECT_EQ(fake.visual_calls, 0);
  EXPECT_EQ(selected.source, SmartRegionDiagnosticSource::ClientArea);
  fake.window_found = false;
  EXPECT_FALSE(detector.detectAt(50, 50, selected));
  EXPECT_FALSE(selected.valid());
}
TEST(SmartRegionBoundaryTest, RejectsExpiredAndPreviousGenerationResults) {
  window_detail::UiaRegionQueryRequest request;
  request.request_id = 7;
  request.generation = 2;
  request.root_window = reinterpret_cast<HWND>(123);
  request.process_id = 456;
  request.owner_rect = {0, 0, 800, 600};
  request.deadline_ms = 1000;
  request.screen_point = {50, 50};
  window_detail::UiaRegionQueryResult result;
  result.request_id = request.request_id;
  result.generation = request.generation;
  result.root_window = request.root_window;
  result.process_id = request.process_id;
  result.owner_rect = request.owner_rect;
  result.deadline_ms = request.deadline_ms;
  result.requested_at_ms = 450;
  FakeProviders fake;
  SmartRegionWindowSnapshot snapshot;
  FakeProviders::snapshot(50, 50, snapshot, &fake);
  result.candidate_count = FakeProviders::accessibility(snapshot, request.screen_point,
      result.candidates, SmartRegionMaxCandidates, &fake);
  EXPECT_TRUE(window_detail::isUiaQueryResultApplicable(result, request, 500));
  result.generation = 1;
  EXPECT_FALSE(window_detail::isUiaQueryResultApplicable(result, request, 500));
  result.generation = 2;
  result.requested_at_ms = 950;
  EXPECT_FALSE(window_detail::isUiaQueryResultApplicable(result, request, 1001));
}

TEST(SmartRegionBoundaryTest, AcceptsOnlyCurrentBrowserShellAtlasSnapshot)
{
  window_detail::UiaRegionQueryRequest request;
  request.request_id = 7;
  request.generation = 2;
  request.root_window = reinterpret_cast<HWND>(123);
  request.process_id = 456;
  request.owner_rect = {0, 0, 800, 600};
  request.deadline_ms = 1000;
  request.screen_point = {50, 50};

  BrowserShellContext context;
  context.m_root_window = request.root_window;
  context.m_process_id = request.process_id;
  context.m_window_rect = request.owner_rect;
  context.m_capture_session_generation = request.generation;
  context.m_window_generation = request.generation;
  BrowserShellAtlas atlas;
  atlas.reset(context);
  BrowserShellEntryCollection entries;
  ASSERT_TRUE(entries.append({{40, 40, 80, 80},
                              BrowserShellRole::ExtensionButton,
                              BrowserShellSource::UiaSemantic, 99, 0, 7, 1}));
  ASSERT_TRUE(atlas.merge(entries, 500));

  window_detail::UiaRegionQueryResult result;
  result.request_id = request.request_id;
  result.generation = request.generation;
  result.root_window = request.root_window;
  result.process_id = request.process_id;
  result.owner_rect = request.owner_rect;
  result.deadline_ms = request.deadline_ms;
  result.requested_at_ms = 500;
  result.m_window_generation = request.generation;
  result.m_browser_shell_atlas = atlas.makeSnapshot();
  EXPECT_TRUE(window_detail::isUiaQueryResultApplicable(result, request, 600));

  ++result.m_window_generation;
  EXPECT_FALSE(window_detail::isUiaQueryResultApplicable(result, request, 600));
}
}  // namespace
TEST(SmartRegionBoundaryTest, ThrowingProviderIsContainedCooledAndDoesNotStopTheWorker) {
  using namespace window_detail;
  std::atomic<int> calls{0};
  UiaRegionQueryWorker worker([](const UiaRegionQueryRequest&, UiaRegionQueryResult& result, void* context) {
    if (++*static_cast<std::atomic<int>*>(context) == 1) throw std::runtime_error("private provider title");
    result.succeeded = true;
  }, &calls);
  ASSERT_TRUE(worker.start());
  UiaRegionQueryRequest request;
  request.request_id = 1; request.generation = 31; request.process_id = 41;
  request.root_window = reinterpret_cast<HWND>(123);
  request.owner_rect = {0, 0, 640, 480}; request.screen_point = {30, 30};
  request.requested_at_ms = GetTickCount64();
  auto wait_result = [&](UiaRegionQueryResult& result) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    do {
      if (worker.tryTakeLatest(result)) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
  };
  ASSERT_TRUE(worker.request(request));
  UiaRegionQueryResult result;
  ASSERT_TRUE(wait_result(result)); EXPECT_FALSE(result.succeeded);
  request.request_id = 2; request.requested_at_ms = GetTickCount64();
  ASSERT_TRUE(worker.request(request)); ASSERT_TRUE(wait_result(result));
  EXPECT_TRUE(result.suppressed_by_cooldown); EXPECT_EQ(calls, 1);
  request.root_window = reinterpret_cast<HWND>(124); request.request_id = 3;
  ASSERT_TRUE(worker.request(request)); ASSERT_TRUE(wait_result(result));
  EXPECT_TRUE(result.succeeded); EXPECT_EQ(calls, 2);
  worker.stop();
}
}  // namespace qingying
