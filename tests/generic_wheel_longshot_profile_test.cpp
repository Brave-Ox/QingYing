#include "generic_wheel_longshot_profile.hpp"

#include "qingying/longshot/longshot_engine.hpp"
#include "qingying/longshot/longshot_profile_registry.hpp"

#include <Windows.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>

namespace qingying {
namespace {

constexpr wchar_t kGenericWheelClass[] =
    L"QingYingGenericWheelLongShotProfileTest";

class ScopedPhysicalCoordinates {
 public:
  ScopedPhysicalCoordinates() {
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) return;
    set_context_ = reinterpret_cast<SetContextFn>(
        GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
    if (set_context_ != nullptr) {
      previous_ = set_context_(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
  }

  ~ScopedPhysicalCoordinates() {
    if (set_context_ != nullptr && previous_ != nullptr) {
      (void)set_context_(previous_);
    }
  }

 private:
  using SetContextFn = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
  SetContextFn set_context_{nullptr};
  DPI_AWARENESS_CONTEXT previous_{nullptr};
};

struct ScrollSurfaceState {
  int offset{0};
  int step{10};
  int maximum{20};
  int wheel_messages{0};
};

LRESULT CALLBACK scrollSurfaceWindowProc(HWND window, UINT message,
                                         WPARAM w_param, LPARAM l_param) {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  } else if (message == WM_MOUSEWHEEL) {
    auto* state = reinterpret_cast<ScrollSurfaceState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (state != nullptr) {
      ++state->wheel_messages;
      state->offset = (std::min)(state->maximum,
                                 state->offset + state->step);
    }
    return 0;
  }
  return DefWindowProcW(window, message, w_param, l_param);
}

bool ensureGenericWheelClass() {
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = scrollSurfaceWindowProc;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = kGenericWheelClass;
  if (RegisterClassExW(&window_class) != 0) return true;
  return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

ScreenPhysicalRect clientScreenRect(HWND window) {
  ScopedPhysicalCoordinates physical_coordinates;
  RECT rect{};
  EXPECT_TRUE(GetClientRect(window, &rect));
  POINT top_left{rect.left, rect.top};
  POINT bottom_right{rect.right, rect.bottom};
  EXPECT_TRUE(ClientToScreen(window, &top_left));
  EXPECT_TRUE(ClientToScreen(window, &bottom_right));
  return {top_left.x, top_left.y, bottom_right.x - top_left.x,
          bottom_right.y - top_left.y};
}

std::uint32_t pixelFor(int x, int global_y) {
  const auto b = static_cast<std::uint32_t>((global_y * 17 + x) & 0xFF);
  const auto g = static_cast<std::uint32_t>((global_y * 31 + x * 3) & 0xFF);
  const auto r = static_cast<std::uint32_t>((global_y * 47 + x * 5) & 0xFF);
  return 0xFF000000u | (r << 16) | (g << 8) | b;
}

Image makeStrip(int width, int height, int first_global_y) {
  Image image;
  image.width = width;
  image.height = height;
  image.pixels.resize(static_cast<std::size_t>(width) * height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      image.pixels[static_cast<std::size_t>(y) * width + x] =
          pixelFor(x, first_global_y + y);
    }
  }
  return image;
}

class GenericWheelWindow {
 public:
  explicit GenericWheelWindow(bool scrollable = true) {
    state_.maximum = scrollable ? 20 : 0;
    if (!ensureGenericWheelClass()) return;
    root_ = CreateWindowExW(
        WS_EX_NOACTIVATE, kGenericWheelClass, L"", WS_POPUP | WS_VISIBLE,
        -12000, -12000, 180, 160, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (root_ == nullptr) return;
    content_ = CreateWindowExW(
        0, kGenericWheelClass, L"", WS_CHILD | WS_VISIBLE, 20, 30, 64, 40,
        root_, nullptr, GetModuleHandleW(nullptr), &state_);
  }

  ~GenericWheelWindow() {
    if (IsWindow(content_)) DestroyWindow(content_);
    if (IsWindow(root_)) DestroyWindow(root_);
  }

  HWND root() const noexcept { return root_; }
  HWND content() const noexcept { return content_; }
  const ScrollSurfaceState& state() const noexcept { return state_; }

  LongShotRequest request() const {
    return {reinterpret_cast<std::uintptr_t>(root_),
            clientScreenRect(content_)};
  }

 private:
  HWND root_{nullptr};
  HWND content_{nullptr};
  ScrollSurfaceState state_;
};

longshot_detail::GenericWheelProfilePolicy testPolicy(int max_inputs = 8) {
  longshot_detail::GenericWheelProfilePolicy policy;
  policy.target_policy.excluded_process_id = 0;
  policy.trusted_ui_process_id = 0;
  policy.enforce_foreground = false;
  policy.max_inputs = max_inputs;
  return policy;
}

}  // namespace

TEST(GenericWheelLongShotProfileTest,
     ControlledWindowScrollsAndProducesVerifiedComposite) {
  GenericWheelWindow window;
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.content(), nullptr);

  auto generic =
      std::make_unique<longshot_detail::GenericWheelLongShotProfile>(
          testPolicy());
  auto* generic_ptr = generic.get();
  LongShotProfileRegistry profiles;
  profiles.add(std::move(generic));
  LongShotLimits limits;
  limits.max_frames = 3;
  LongShotEngine engine(
      [&](const ScreenPhysicalRect& region, Image& image) {
        image = makeStrip(region.width, region.height, window.state().offset);
        ActionResult result;
        result.ok = true;
        result.error_code = ErrorCode::kOk;
        return result;
      },
      std::move(profiles), limits);
  LongShotOutcome outcome;
  const LongShotRequest request = window.request();

  const ActionResult result = engine.captureSelection(request, outcome);

  EXPECT_TRUE(result.ok);
  EXPECT_EQ(outcome.strategy, "generic.wheel");
  EXPECT_EQ(outcome.stop_reason, LongShotStopReason::LimitReached);
  EXPECT_EQ(outcome.accepted_frames, 3);
  EXPECT_EQ(outcome.input_attempts, 2);
  EXPECT_EQ(outcome.recapture_attempts, 2);
  EXPECT_EQ(outcome.image.width, request.width);
  EXPECT_EQ(outcome.image.height, request.height + window.state().maximum);
  EXPECT_TRUE(outcome.isPartial());
  EXPECT_EQ(window.state().wheel_messages, 2);
  EXPECT_EQ(generic_ptr->inputCount(), 2);
}

TEST(GenericWheelLongShotProfileTest,
     NonScrollableWindowStopsAsNoProgressAfterOneInput) {
  GenericWheelWindow window(false);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.content(), nullptr);
  auto generic =
      std::make_unique<longshot_detail::GenericWheelLongShotProfile>(
          testPolicy());
  auto* generic_ptr = generic.get();
  LongShotProfileRegistry profiles;
  profiles.add(std::move(generic));
  int captures = 0;
  LongShotEngine engine(
      [&](const ScreenPhysicalRect& region, Image& image) {
        ++captures;
        image = makeStrip(region.width, region.height, window.state().offset);
        ActionResult result;
        result.ok = true;
        result.error_code = ErrorCode::kOk;
        return result;
      },
      std::move(profiles));
  LongShotOutcome outcome;

  const ActionResult result = engine.captureSelection(window.request(), outcome);

  EXPECT_TRUE(result.ok);
  EXPECT_EQ(outcome.stop_reason, LongShotStopReason::NoProgress);
  EXPECT_EQ(outcome.accepted_frames, 1);
  EXPECT_EQ(outcome.input_attempts, 1);
  EXPECT_EQ(outcome.recapture_attempts, 2);
  EXPECT_EQ(window.state().wheel_messages, 1);
  EXPECT_EQ(generic_ptr->inputCount(), 1);
  EXPECT_EQ(captures, 4);
}

TEST(GenericWheelLongShotProfileTest,
     RejectsForeignTargetWithoutSendingAnyInput) {
  GenericWheelWindow owner;
  GenericWheelWindow foreign;
  ASSERT_NE(owner.content(), nullptr);
  ASSERT_NE(foreign.content(), nullptr);
  longshot_detail::GenericWheelLongShotProfile profile(testPolicy());
  LongShotProfileResult resolved;
  const LongShotRequest request = owner.request();
  ASSERT_TRUE(profile.resolve(request, resolved));
  LongShotProfileResult forged = resolved;
  forged.scroll_target =
      reinterpret_cast<std::uintptr_t>(foreign.content());

  EXPECT_FALSE(profile.scrollDown(request, forged));
  EXPECT_EQ(owner.state().wheel_messages, 0);
  EXPECT_EQ(foreign.state().wheel_messages, 0);
  EXPECT_EQ(profile.inputCount(), 0);
}

TEST(GenericWheelLongShotProfileTest,
     RevalidatesMovedTargetBeforeSendingInput) {
  GenericWheelWindow window;
  ASSERT_NE(window.content(), nullptr);
  longshot_detail::GenericWheelLongShotProfile profile(testPolicy());
  const LongShotRequest request = window.request();
  LongShotProfileResult resolved;
  ASSERT_TRUE(profile.resolve(request, resolved));

  ASSERT_TRUE(SetWindowPos(window.content(), nullptr, 21, 30, 64, 40,
                           SWP_NOACTIVATE | SWP_NOZORDER));

  EXPECT_FALSE(profile.scrollDown(request, resolved));
  EXPECT_EQ(window.state().wheel_messages, 0);
  EXPECT_EQ(profile.inputCount(), 0);
}

TEST(GenericWheelLongShotProfileTest,
     FocusSafetyRejectsWindowOutsideRecordedOwner) {
  GenericWheelWindow owner;
  GenericWheelWindow foreign;
  ASSERT_NE(owner.content(), nullptr);
  ASSERT_NE(foreign.content(), nullptr);
  const auto resolution = longshot_detail::resolveGenericScrollTarget(
      owner.request(), {0});
  ASSERT_TRUE(resolution.resolved());

  EXPECT_TRUE(longshot_detail::genericWheelForegroundIsSafe(
      resolution.target, 0,
      reinterpret_cast<std::uintptr_t>(owner.root())));
  EXPECT_TRUE(longshot_detail::genericWheelForegroundIsSafe(
      resolution.target, 0,
      reinterpret_cast<std::uintptr_t>(owner.content())));
  EXPECT_FALSE(longshot_detail::genericWheelForegroundIsSafe(
      resolution.target, 0,
      reinterpret_cast<std::uintptr_t>(foreign.root())));
}

TEST(GenericWheelLongShotProfileTest, InputBudgetIsStrictlyBounded) {
  GenericWheelWindow window;
  ASSERT_NE(window.content(), nullptr);
  longshot_detail::GenericWheelLongShotProfile profile(testPolicy(1));
  const LongShotRequest request = window.request();
  LongShotProfileResult resolved;
  ASSERT_TRUE(profile.resolve(request, resolved));

  EXPECT_TRUE(profile.scrollDown(request, resolved));
  EXPECT_FALSE(profile.scrollDown(request, resolved));
  EXPECT_EQ(window.state().wheel_messages, 1);
  EXPECT_EQ(profile.inputCount(), 1);
  LongShotScrollState state;
  EXPECT_FALSE(profile.queryScrollState(resolved, state));
  EXPECT_FALSE(state.valid);
}

}  // namespace qingying
