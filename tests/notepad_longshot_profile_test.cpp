#include "qingying/longshot/notepad_longshot_profile.hpp"

#include "qingying/capture/capture_engine.hpp"
#include "qingying/longshot/longshot_engine.hpp"

#include <Windows.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace qingying {

namespace {

constexpr wchar_t kNotepadWindowClass[] = L"Notepad";
constexpr wchar_t kUnsupportedWindowClass[] = L"QingYingUnsupportedWindow";
constexpr wchar_t kTrackedEditorWindowClass[] = L"RichEditQingYingTest";

LRESULT CALLBACK trackedEditorWindowProc(HWND window, UINT message,
                                         WPARAM w_param, LPARAM l_param) {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  } else if (message == WM_MOUSEWHEEL) {
    auto* wheel_count =
        reinterpret_cast<int*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (wheel_count != nullptr) {
      ++(*wheel_count);
    }
  }
  return DefWindowProcW(window, message, w_param, l_param);
}

bool ensureWindowClass(const wchar_t* class_name,
                       WNDPROC window_proc = DefWindowProcW) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = window_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = class_name;
  if (RegisterClassExW(&wc) != 0) {
    return true;
  }
  return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

class TestEditorWindow {
 public:
  explicit TestEditorWindow(const wchar_t* root_class,
                            bool track_wheel = false) {
    if (!ensureWindowClass(root_class)) {
      return;
    }

    root_ = CreateWindowExW(WS_EX_NOACTIVATE, root_class, L"",
                            WS_OVERLAPPED | WS_VISIBLE, -10000, -10000, 500,
                            400, nullptr, nullptr, GetModuleHandleW(nullptr),
                            nullptr);
    if (root_ == nullptr) {
      return;
    }

    const wchar_t* editor_class = L"Edit";
    DWORD editor_style =
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | WS_VSCROLL;
    void* creation_parameter = nullptr;
    if (track_wheel) {
      if (!ensureWindowClass(kTrackedEditorWindowClass,
                             trackedEditorWindowProc)) {
        return;
      }
      editor_class = kTrackedEditorWindowClass;
      editor_style = WS_CHILD | WS_VISIBLE | WS_VSCROLL;
      creation_parameter = &wheel_message_count_;
    }

    editor_ = CreateWindowExW(0, editor_class, L"", editor_style, 20, 30, 300,
                              180, root_, nullptr, GetModuleHandleW(nullptr),
                              creation_parameter);
  }

  ~TestEditorWindow() {
    if (editor_ != nullptr) {
      DestroyWindow(editor_);
    }
    if (root_ != nullptr) {
      DestroyWindow(root_);
    }
  }

  HWND root() const { return root_; }
  HWND editor() const { return editor_; }
  int wheelMessageCount() const { return wheel_message_count_; }

  RECT editorScreenRect() const {
    RECT rect{};
    GetClientRect(editor_, &rect);
    POINT top_left{rect.left, rect.top};
    POINT bottom_right{rect.right, rect.bottom};
    ClientToScreen(editor_, &top_left);
    ClientToScreen(editor_, &bottom_right);
    return {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
  }

 private:
  HWND root_{nullptr};
  HWND editor_{nullptr};
  int wheel_message_count_{0};
};

}  // namespace

TEST(NotepadLongShotProfileTest, ResolvesSuppliedNotepadEditorClientArea) {
  TestEditorWindow window(kNotepadWindowClass);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.editor(), nullptr);

  LongShotProfileResult profile;
  ASSERT_TRUE(resolveNotepadProfile(
      reinterpret_cast<std::uintptr_t>(window.root()), profile));

  const RECT expected = window.editorScreenRect();
  EXPECT_EQ(profile.scroll_target,
            reinterpret_cast<std::uintptr_t>(window.editor()));
  EXPECT_EQ(profile.content_x, expected.left);
  EXPECT_EQ(profile.content_y, expected.top);
  EXPECT_EQ(profile.content_width, expected.right - expected.left);
  EXPECT_EQ(profile.content_height, expected.bottom - expected.top);
}

TEST(NotepadLongShotProfileTest, ExactContentBoundsAreAccepted) {
  const LongShotProfileResult profile{1, -200, 100, 640, 480};

  EXPECT_TRUE(profile.containsSelection(-200, 100, 640, 480));
  EXPECT_TRUE(profile.containsSelection(-100, 200, 100, 100));
}

TEST(NotepadLongShotProfileTest, CrossingAnyContentEdgeIsRejected) {
  const LongShotProfileResult profile{1, 100, 200, 640, 480};

  EXPECT_FALSE(profile.containsSelection(99, 200, 640, 480));
  EXPECT_FALSE(profile.containsSelection(100, 199, 640, 480));
  EXPECT_FALSE(profile.containsSelection(100, 200, 641, 480));
  EXPECT_FALSE(profile.containsSelection(100, 200, 640, 481));
}

TEST(NotepadLongShotProfileTest, InvalidOrOverflowingSelectionIsRejected) {
  const LongShotProfileResult profile{1, 0, 0, 640, 480};

  EXPECT_FALSE(profile.containsSelection(0, 0, 0, 100));
  EXPECT_FALSE(profile.containsSelection(0, 0, 100, 0));
  EXPECT_FALSE(profile.containsSelection((std::numeric_limits<int>::max)(), 0,
                                         1, 1));
  EXPECT_FALSE(profile.containsSelection(0, (std::numeric_limits<int>::max)(),
                                         1, 1));
}

TEST(NotepadLongShotProfileTest, InvalidWindowClearsPreviousResult) {
  LongShotProfileResult profile{1, 10, 20, 30, 40};

  EXPECT_FALSE(resolveNotepadProfile(0, profile));
  EXPECT_FALSE(profile.valid());
  EXPECT_EQ(profile.scroll_target, 0u);
}

TEST(NotepadLongShotProfileTest, NonNotepadWindowIsRejected) {
  TestEditorWindow window(kUnsupportedWindowClass);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.editor(), nullptr);

  LongShotProfileResult profile{1, 10, 20, 30, 40};
  EXPECT_FALSE(resolveNotepadProfile(
      reinterpret_cast<std::uintptr_t>(window.root()), profile));
  EXPECT_FALSE(profile.valid());
}

TEST(NotepadLongShotProfileTest, ChildHandleCannotReplaceRecordedRootWindow) {
  TestEditorWindow window(kNotepadWindowClass);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.editor(), nullptr);

  LongShotProfileResult profile;
  EXPECT_FALSE(resolveNotepadProfile(
      reinterpret_cast<std::uintptr_t>(window.editor()), profile));
  EXPECT_FALSE(profile.valid());
}

TEST(LongShotEngineProfileIntegrationTest,
     SupportedSelectionStopsBeforeCaptureAndScroll) {
  TestEditorWindow window(kNotepadWindowClass);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.editor(), nullptr);

  LongShotProfileResult profile;
  ASSERT_TRUE(resolveNotepadProfile(
      reinterpret_cast<std::uintptr_t>(window.root()), profile));

  CaptureEngine capture;
  LongShotEngine engine(capture);
  const LongShotRequest request{
      reinterpret_cast<std::uintptr_t>(window.root()), profile.content_x,
      profile.content_y, profile.content_width, profile.content_height};
  Image out;

  const ActionResult result = engine.captureSelection(request, out);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, ErrorCode::kNotImplemented);
  EXPECT_TRUE(out.empty());
}

TEST(LongShotEngineProfileIntegrationTest, SelectionOutsideContentIsRejected) {
  TestEditorWindow window(kNotepadWindowClass);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.editor(), nullptr);

  LongShotProfileResult profile;
  ASSERT_TRUE(resolveNotepadProfile(
      reinterpret_cast<std::uintptr_t>(window.root()), profile));

  CaptureEngine capture;
  LongShotEngine engine(capture);
  const LongShotRequest request{
      reinterpret_cast<std::uintptr_t>(window.root()), profile.content_x - 1,
      profile.content_y, profile.content_width, profile.content_height};
  Image out;

  const ActionResult result = engine.captureSelection(request, out);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, ErrorCode::kLongShotUnsupported);
  EXPECT_TRUE(out.empty());
}

TEST(LongShotInitialPairTest, CapturesFixedRectAroundExactlyOneWheelInput) {
  TestEditorWindow window(kNotepadWindowClass, true);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.editor(), nullptr);

  LongShotProfileResult profile;
  ASSERT_TRUE(resolveNotepadProfile(
      reinterpret_cast<std::uintptr_t>(window.root()), profile));

  constexpr int kSelectionWidth = 80;
  constexpr int kSelectionHeight = 60;
  const LongShotRequest request{
      reinterpret_cast<std::uintptr_t>(window.root()), profile.content_x + 10,
      profile.content_y + 10, kSelectionWidth, kSelectionHeight};
  CaptureEngine capture;
  LongShotEngine engine(capture);
  LongShotFramePair frames;

  const ActionResult result = engine.captureInitialPair(request, frames);

  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.error_code, ErrorCode::kOk);
  EXPECT_TRUE(frames.valid());
  EXPECT_EQ(frames.first_frame.width, kSelectionWidth);
  EXPECT_EQ(frames.first_frame.height, kSelectionHeight);
  EXPECT_EQ(frames.second_frame.width, kSelectionWidth);
  EXPECT_EQ(frames.second_frame.height, kSelectionHeight);
  EXPECT_EQ(frames.first_frame.pixels.size(),
            static_cast<std::size_t>(kSelectionWidth * kSelectionHeight));
  EXPECT_EQ(frames.second_frame.pixels.size(),
            static_cast<std::size_t>(kSelectionWidth * kSelectionHeight));
  EXPECT_EQ(window.wheelMessageCount(), 1);
}

TEST(LongShotInitialPairTest, InvalidSelectionDoesNotScrollAndClearsFrames) {
  TestEditorWindow window(kNotepadWindowClass, true);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.editor(), nullptr);

  LongShotProfileResult profile;
  ASSERT_TRUE(resolveNotepadProfile(
      reinterpret_cast<std::uintptr_t>(window.root()), profile));

  const LongShotRequest request{
      reinterpret_cast<std::uintptr_t>(window.root()), profile.content_x - 1,
      profile.content_y, profile.content_width, profile.content_height};
  CaptureEngine capture;
  LongShotEngine engine(capture);
  LongShotFramePair frames;
  frames.first_frame = Image{1, 1, {0xFFFFFFFFu}};
  frames.second_frame = Image{1, 1, {0xFFFFFFFFu}};

  const ActionResult result = engine.captureInitialPair(request, frames);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, ErrorCode::kLongShotUnsupported);
  EXPECT_TRUE(frames.first_frame.empty());
  EXPECT_TRUE(frames.second_frame.empty());
  EXPECT_EQ(window.wheelMessageCount(), 0);
}

}  // namespace qingying
