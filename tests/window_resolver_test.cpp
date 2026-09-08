#include "qingying/window/window_resolver.h"
#include "window_query_helpers.h"

#include <gtest/gtest.h>

namespace qingying {
namespace {
WindowCatalogEntry entry(std::uintptr_t handle, std::uint32_t pid,
                         std::wstring title, ScreenPhysicalRect bounds = {1, 2, 30, 40}) {
  return {handle, pid, std::move(title), bounds};
}

TEST(WindowResolverTest, ReturnsNotFoundAndRejectsEmptyQuery) {
  WindowResolver resolver([] { return WindowCatalogSnapshot{}; });
  EXPECT_EQ(resolver.resolve({}).error_code, ErrorCode::kInvalidArgument);
  EXPECT_EQ(resolver.resolve({L"missing"}).error_code, ErrorCode::kWindowNotFound);
}

TEST(WindowResolverTest, MatchesChineseCaseAndOptionalPid) {
  WindowCatalogSnapshot snapshot{{entry(1, 10, L"轻映 - EDITOR"),
                                  entry(2, 20, L"轻映 - editor")}};
  WindowResolver resolver([snapshot] { return snapshot; });
  WindowQuery query{L"轻映 - editor", WindowTitleMatch::Exact, 20};
  const auto result = resolver.resolve(query);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.window.has_value());
  EXPECT_EQ(result.window->identity.native_handle, 2u);
  EXPECT_EQ(result.candidates.candidates.front().window_token,
            result.window->window_token);
}

TEST(WindowResolverTest, ContainsIsInvariantAndDoesNotInterpretRegex) {
  WindowResolver resolver([] { return WindowCatalogSnapshot{{
      entry(1, 10, L"Report [final] - QINGYING")}}; });
  EXPECT_TRUE(resolver.resolve({L"[FINAL]", WindowTitleMatch::Contains}).ok());
  EXPECT_EQ(resolver.resolve({L".*", WindowTitleMatch::Contains}).error_code,
            ErrorCode::kWindowNotFound);
}

TEST(WindowResolverTest, AmbiguousCandidatesAreStableCappedAndOpaque) {
  WindowResolver resolver([] { return WindowCatalogSnapshot{{
      entry(30, 3, L"beta"), entry(20, 2, L"Alpha"),
      entry(10, 1, L"alpha")}}; }, 2);
  const auto result = resolver.resolve({L"a"});
  EXPECT_EQ(result.error_code, ErrorCode::kWindowAmbiguous);
  ASSERT_EQ(result.candidates.candidates.size(), 2u);
  EXPECT_TRUE(result.candidates.truncated);
  EXPECT_EQ(result.candidates.candidates[0].process_id, 1u);
  EXPECT_EQ(result.candidates.candidates[1].process_id, 2u);
  EXPECT_EQ(result.candidates.candidates[0].window_token.find("0x"),
            std::string::npos);
}

TEST(WindowResolverTest, TruncatedCatalogCannotClaimUniqueMatch) {
  WindowResolver resolver([] { return WindowCatalogSnapshot{{
      entry(1, 1, L"only")}, true}; });
  const auto result = resolver.resolve({L"only", WindowTitleMatch::Exact});
  EXPECT_EQ(result.error_code, ErrorCode::kWindowAmbiguous);
  EXPECT_TRUE(result.candidates.truncated);
}

TEST(WindowResolverTest, RevalidateDetectsCloseIdentityAndBoundsChanges) {
  auto current = WindowCatalogSnapshot{{entry(7, 8, L"窗口", {-100, 0, 80, 60})}};
  WindowResolver resolver([&current] { return current; });
  const auto resolved = resolver.resolve({L"窗口", WindowTitleMatch::Exact});
  ASSERT_TRUE(resolved.ok());
  EXPECT_TRUE(resolver.revalidate(*resolved.window));
  current.entries[0].process_id = 9;
  EXPECT_FALSE(resolver.revalidate(*resolved.window));
  current.entries[0].process_id = 8;
  current.entries[0].bounds.x = -99;
  EXPECT_FALSE(resolver.revalidate(*resolved.window));
  current.entries.clear();
  EXPECT_FALSE(resolver.revalidate(*resolved.window));
}

TEST(WindowQueryHelpersTest, FallsBackWhenDwmBoundsAreInvalid) {
  RECT result{};
  const auto invalid_dwm = [](HWND, RECT& r) { r = {1, 1, 1, 1}; return true; };
  const auto fallback = [](HWND, RECT& r) { r = {-20, 10, 80, 90}; return true; };
  ASSERT_TRUE(window_detail::readVisibleBounds(nullptr, result, invalid_dwm, fallback));
  EXPECT_EQ(result.left, -20);
  EXPECT_EQ(result.right, 80);
}

TEST(WindowQueryHelpersTest, F2PolicyAllowsPartiallyOffscreenWindow) {
  const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  RECT partial{left - 10, top, left + 10, top + 20};
  EXPECT_TRUE(window_detail::intersectsVirtualDesktop(partial));
  EXPECT_FALSE(window_detail::fullyInsideVirtualDesktop(partial));
}
}  // namespace
}  // namespace qingying
