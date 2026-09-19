#include <Windows.h>

#include <gtest/gtest.h>

#include <cwchar>

#include "qingying/app/app_messages.hpp"
#include "qingying/overlay/selection_overlay.hpp"
#include "qingying/overlay/selection_toolbar.hpp"
#include "qingying/ui/shortcut_types.hpp"

namespace qingying {
namespace {

struct WindowSearchContext
{
  DWORD process_id{0};
  const wchar_t* class_name{nullptr};
  HWND window{nullptr};
};

BOOL CALLBACK findProcessWindow(HWND hwnd, LPARAM lparam)
{
  WindowSearchContext* context =
      reinterpret_cast<WindowSearchContext*>(lparam);
  if (context == nullptr || context->class_name == nullptr)
  {
    return FALSE;
  }

  DWORD process_id = 0;
  static_cast<void>(GetWindowThreadProcessId(hwnd, &process_id));
  if (process_id != context->process_id)
  {
    return TRUE;
  }

  wchar_t class_name[128]{};
  if (GetClassNameW(hwnd, class_name, ARRAYSIZE(class_name)) <= 0 ||
      std::wcscmp(class_name, context->class_name) != 0)
  {
    return TRUE;
  }

  context->window = hwnd;
  return FALSE;
}

HWND findCurrentProcessWindow(const wchar_t* class_name)
{
  WindowSearchContext context{GetCurrentProcessId(), class_name, nullptr};
  static_cast<void>(
      EnumWindows(findProcessWindow, reinterpret_cast<LPARAM>(&context)));
  return context.window;
}

void dispatchCurrentThreadMessages()
{
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
  {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

void selectRegion(HWND hwnd)
{
  SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 30));
  SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(180, 150));
  SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(180, 150));
}

class ScopedThreadHotkey
{
 public:
  ScopedThreadHotkey(int hotkey_id, UINT modifiers, UINT virtual_key)
      : m_hotkey_id(hotkey_id),
        m_registered(RegisterHotKey(nullptr, hotkey_id, modifiers,
                                   virtual_key) != FALSE)
  {
  }

  ~ScopedThreadHotkey()
  {
    if (m_registered)
    {
      static_cast<void>(UnregisterHotKey(nullptr, m_hotkey_id));
    }
  }

  bool registered() const noexcept
  {
    return m_registered;
  }

 private:
  int m_hotkey_id{0};
  bool m_registered{false};
};

TEST(SelectionOverlayTest, ShowReturnsWithoutBlockingAndHideIsSilent) {
  SelectionOverlay overlay;
  bool callback_invoked = false;

  ASSERT_TRUE(overlay.show(
      Image{},
      [&callback_invoked](const SelectionResult&) {
        callback_invoked = true;
      }));
  EXPECT_TRUE(overlay.isVisible());

  overlay.hide();
  EXPECT_FALSE(overlay.isVisible());
  EXPECT_FALSE(callback_invoked);
}

TEST(SelectionOverlayTest, CustomShortcutSnapshotRoutesCopyAction)
{
  SelectionOverlay overlay;
  SelectionIntent result;
  bool callback_invoked = false;
  const SelectionShortcutSettings shortcuts{
      ShortcutBinding{MOD_ALT, static_cast<UINT>('X')}, ShortcutBinding{}};

  ASSERT_TRUE(overlay.show(
      Image{},
      [&result, &callback_invoked](const SelectionIntent& selection)
      {
        result = selection;
        callback_invoked = true;
      },
      {}, {}, {}, false, shortcuts));
  const HWND hwnd = FindWindowW(L"QingYingSelectionOverlay", nullptr);
  ASSERT_NE(hwnd, nullptr);

  selectRegion(hwnd);
  SendMessageW(hwnd, WM_HOTKEY,
               static_cast<WPARAM>(SelectionToolbarCopyHotkeyId), 0);
  dispatchCurrentThreadMessages();

  ASSERT_TRUE(callback_invoked);
  EXPECT_EQ(result.action, SelectionAction::Copy);
}

TEST(SelectionOverlayTest, EmptyShortcutSnapshotDoesNotInvokeToolbarAction)
{
  SelectionOverlay overlay;
  bool callback_invoked = false;
  const SelectionShortcutSettings shortcuts{};

  ASSERT_TRUE(overlay.show(
      Image{},
      [&callback_invoked](const SelectionIntent&)
      {
        callback_invoked = true;
      },
      {}, {}, {}, false, shortcuts));
  const HWND hwnd = FindWindowW(L"QingYingSelectionOverlay", nullptr);
  ASSERT_NE(hwnd, nullptr);

  ScopedThreadHotkey available_hotkey(
      96, MOD_ALT | MOD_NOREPEAT, VK_F24);
  EXPECT_TRUE(available_hotkey.registered());

  selectRegion(hwnd);
  SendMessageW(hwnd, WM_HOTKEY,
               static_cast<WPARAM>(SelectionToolbarCopyHotkeyId), 0);
  dispatchCurrentThreadMessages();

  EXPECT_TRUE(overlay.isVisible());
  EXPECT_FALSE(callback_invoked);
  overlay.hide();
}

TEST(SelectionOverlayTest, EmptyShortcutSnapshotPreservesEscapeBehavior)
{
  SelectionOverlay overlay;
  bool callback_invoked = false;

  ASSERT_TRUE(overlay.show(
      Image{},
      [&callback_invoked](const SelectionIntent&)
      {
        callback_invoked = true;
      },
      {}, {}, {}, false, SelectionShortcutSettings{}));
  const HWND hwnd = FindWindowW(L"QingYingSelectionOverlay", nullptr);
  ASSERT_NE(hwnd, nullptr);

  SendMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
  dispatchCurrentThreadMessages();

  EXPECT_FALSE(overlay.isVisible());
  EXPECT_TRUE(callback_invoked);
}

TEST(SelectionOverlayTest, StolenLocalShortcutKeepsMouseSelectionUsable)
{
  constexpr int StolenHotkeyId = 97;
  ScopedThreadHotkey stolen_hotkey(
      StolenHotkeyId, MOD_ALT | MOD_NOREPEAT, VK_F24);
  ASSERT_TRUE(stolen_hotkey.registered());

  SelectionOverlay overlay;
  bool callback_invoked = false;
  const SelectionShortcutSettings shortcuts{
      ShortcutBinding{MOD_ALT, VK_F24}, ShortcutBinding{}};
  ASSERT_TRUE(overlay.show(
      Image{},
      [&callback_invoked](const SelectionIntent&)
      {
        callback_invoked = true;
      },
      {}, {}, {}, false, shortcuts));
  const HWND hwnd = FindWindowW(L"QingYingSelectionOverlay", nullptr);
  ASSERT_NE(hwnd, nullptr);

  selectRegion(hwnd);
  SendMessageW(hwnd, WM_HOTKEY,
               static_cast<WPARAM>(SelectionToolbarCopyHotkeyId), 0);
  dispatchCurrentThreadMessages();

  EXPECT_TRUE(overlay.isVisible());
  EXPECT_FALSE(callback_invoked);
  overlay.hide();
}

TEST(SelectionOverlayTest, DestructorClosesVisibleOverlayWithoutCallback) {
  int callback_count = 0;
  {
    SelectionOverlay overlay;
    ASSERT_TRUE(overlay.show(
        Image{}, [&callback_count](const SelectionResult&) {
          ++callback_count;
        }));
    EXPECT_TRUE(overlay.isVisible());
  }

  EXPECT_EQ(callback_count, 0);
}

TEST(SelectionOverlayTest, HoverInfrastructureDoesNotChangeSilentHide)
{
  SelectionOverlay overlay;
  int callback_count = 0;
  ASSERT_TRUE(overlay.show(
      Image{}, [&callback_count](const SelectionIntent&) {
        ++callback_count;
      }));

  overlay.hide();

  EXPECT_FALSE(overlay.isVisible());
  EXPECT_EQ(callback_count, 0);
}

TEST(SelectionOverlayTest, QueuedLongShotMessagesCanBeAbortedSafely) {
  SelectionOverlay overlay;
  ASSERT_TRUE(overlay.show(Image{}, [](const SelectionResult&) {}));

  Image preview;
  preview.width = 1;
  preview.height = 1;
  preview.pixels = {0xFFFFFFFFu};
  EXPECT_TRUE(overlay.postLongShotPreview(preview));
  EXPECT_TRUE(overlay.postLongShotFinished(true));

  overlay.hide();
  overlay.hide();
  EXPECT_FALSE(overlay.isVisible());
}

TEST(SelectionOverlayTest, PendingPreviewUsesLatestImageAndReleasesBudgetOnHide) {
  SelectionOverlay overlay;
  ASSERT_TRUE(overlay.show(Image{}, [](const SelectionResult&) {}));
  Image preview{1, 1, {0xFFFFFFFFu}};
  const auto baseline = ImageMemoryBudget::global().snapshot().used_bytes;
  for (int i = 0; i < 100; ++i) ASSERT_TRUE(overlay.postLongShotPreview(preview));
  const auto pending = ImageMemoryBudget::global().snapshot();
  EXPECT_EQ(pending.bytes[static_cast<std::size_t>(ImageMemoryKind::Preview)], 4u);
  EXPECT_EQ(pending.allocations[static_cast<std::size_t>(ImageMemoryKind::Preview)], 1u);
  EXPECT_EQ(pending.used_bytes, baseline + 4);
  overlay.hide();
  EXPECT_EQ(ImageMemoryBudget::global().snapshot().used_bytes, baseline);
}

TEST(SelectionOverlayTest, ExpiredPendingPreviewReturnsPixelBudgetInsteadOfDisplaying) {
  SelectionOverlay overlay;
  ASSERT_TRUE(overlay.show(Image{}, [](const SelectionResult&) {}));
  Image preview{1, 1, {0xFFFFFFFFu}};
  ASSERT_TRUE(overlay.postLongShotPreview(preview));
  Sleep(2100);
  const HWND window = FindWindowW(L"QingYingSelectionOverlay", nullptr);
  ASSERT_NE(window, nullptr);
  MSG message{};
  while (PeekMessageW(&message, window, WM_QINGYING_SELECTION_LONGSHOT_PREVIEW,
                      WM_QINGYING_SELECTION_LONGSHOT_PREVIEW, PM_REMOVE)) {
    DispatchMessageW(&message);
  }
  EXPECT_EQ(ImageMemoryBudget::global().snapshot().bytes[
                static_cast<std::size_t>(ImageMemoryKind::Preview)], 0u);
  overlay.hide();
}

TEST(SelectionOverlayTest, SessionChangeDiscardsOldPreviewAndRejectsOldProducer) {
  SelectionOverlay overlay;
  ASSERT_TRUE(overlay.show(Image{}, [](const SelectionResult&) {}));
  Image preview{1, 1, {0xFFFFFFFFu}};
  const auto baseline = ImageMemoryBudget::global().snapshot().used_bytes;
  overlay.setLongShotSession(10);
  ASSERT_TRUE(overlay.postLongShotPreview(preview, 10));
  overlay.setLongShotSession(11);
  EXPECT_EQ(ImageMemoryBudget::global().snapshot().used_bytes, baseline);
  EXPECT_FALSE(overlay.postLongShotPreview(preview, 10));
  ASSERT_TRUE(overlay.postLongShotPreview(preview, 11));
  EXPECT_EQ(ImageMemoryBudget::global().snapshot().used_bytes, baseline + 4);
  overlay.hide();
  EXPECT_EQ(ImageMemoryBudget::global().snapshot().used_bytes, baseline);
}

TEST(SelectionOverlayTest, WindowDestroyedBeforeOwnerIsSafe) {
  SelectionOverlay overlay;
  ASSERT_TRUE(overlay.show(Image{}, [](const SelectionResult&) {}));

  const HWND hwnd = FindWindowW(L"QingYingSelectionOverlay", nullptr);
  ASSERT_NE(hwnd, nullptr);
  ASSERT_TRUE(DestroyWindow(hwnd));
  EXPECT_FALSE(overlay.isVisible());

  overlay.hide();
  ASSERT_TRUE(overlay.show(Image{}, [](const SelectionResult&) {}));
  overlay.hide();
}

TEST(SelectionOverlayTest, ManualDragRemainsTheFinalSelection)
{
  SelectionOverlay overlay;
  SelectionIntent result;
  bool callback_invoked = false;
  ASSERT_TRUE(overlay.show(
      Image{},
      [&result, &callback_invoked](const SelectionIntent& selection)
      {
        result = selection;
        callback_invoked = true;
      }));
  const HWND hwnd = FindWindowW(L"QingYingSelectionOverlay", nullptr);
  ASSERT_NE(hwnd, nullptr);

  SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 30));
  SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(180, 150));
  SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(180, 150));
  SendMessageW(hwnd, WM_CLOSE, 0, 0);

  ASSERT_TRUE(callback_invoked);
  EXPECT_FALSE(result.cancelled);
  EXPECT_EQ(result.width, 160);
  EXPECT_EQ(result.height, 120);
}

TEST(SelectionOverlayTest, FirstFrameQueryTurnsTrueOnlyAfterLayeredCommit)
{
  SelectionOverlay overlay;
  ASSERT_TRUE(overlay.show(Image{}, [](const SelectionResult&) {}));
  const HWND hwnd =
      findCurrentProcessWindow(L"QingYingSelectionOverlay");
  ASSERT_NE(hwnd, nullptr);

  EXPECT_EQ(SendMessageW(
                hwnd, WM_QINGYING_SELECTION_OVERLAY_FIRST_FRAME_QUERY, 0, 0),
            0);

  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
  {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  EXPECT_EQ(SendMessageW(
                hwnd, WM_QINGYING_SELECTION_OVERLAY_FIRST_FRAME_QUERY, 0, 0),
            1);
  overlay.hide();
}

}  // namespace
}  // namespace qingying
