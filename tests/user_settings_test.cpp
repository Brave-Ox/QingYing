#include "qingying/app/user_settings.hpp"

#include <Windows.h>

#include <gtest/gtest.h>

namespace qingying {
namespace {

TEST(UserSettingsTest, DefaultsProvideUsableCaptureAndSelectionShortcuts)
{
  const UserSettings settings = defaultUserSettings();

  EXPECT_EQ(settings.m_schema_version, UserSettingsSchemaVersion);
  EXPECT_EQ(settings.m_capture_hotkey.m_modifiers, MOD_CONTROL | MOD_SHIFT);
  EXPECT_EQ(settings.m_capture_hotkey.m_virtual_key, static_cast<UINT>('Q'));
  EXPECT_EQ(settings.m_selection_shortcuts.m_copy.m_modifiers, MOD_CONTROL);
  EXPECT_EQ(settings.m_selection_shortcuts.m_copy.m_virtual_key,
            static_cast<UINT>('C'));
  EXPECT_EQ(settings.m_selection_shortcuts.m_toggle_longshot.m_modifiers, 0u);
  EXPECT_EQ(settings.m_selection_shortcuts.m_toggle_longshot.m_virtual_key,
            static_cast<UINT>('L'));
  EXPECT_EQ(settings.m_longshot_limits.max_frames, 30);
  EXPECT_EQ(settings.m_longshot_limits.max_output_height, 30000);
  EXPECT_TRUE(validateUserSettings(settings).valid());
}

TEST(UserSettingsTest, RejectsOutOfRangeLongShotLimits)
{
  UserSettings settings = defaultUserSettings();

  settings.m_longshot_limits.max_frames = 1;
  EXPECT_FALSE(validateUserSettings(settings).m_longshot_limits_valid);

  settings = defaultUserSettings();
  settings.m_longshot_limits.max_frames = 61;
  EXPECT_FALSE(validateUserSettings(settings).m_longshot_limits_valid);

  settings = defaultUserSettings();
  settings.m_longshot_limits.max_output_height = 2999;
  EXPECT_FALSE(validateUserSettings(settings).m_longshot_limits_valid);

  settings = defaultUserSettings();
  settings.m_longshot_limits.max_output_height = 60001;
  EXPECT_FALSE(validateUserSettings(settings).m_longshot_limits_valid);
}

TEST(UserSettingsTest, RejectsInvalidShortcutBindings)
{
  UserSettings settings = defaultUserSettings();

  settings.m_capture_hotkey = ShortcutBinding{};
  EXPECT_FALSE(validateUserSettings(settings).m_capture_hotkey_valid);

  settings = defaultUserSettings();
  settings.m_capture_hotkey = ShortcutBinding{0, static_cast<UINT>('Q')};
  EXPECT_FALSE(validateUserSettings(settings).m_capture_hotkey_valid);

  settings = defaultUserSettings();
  settings.m_selection_shortcuts.m_copy =
      ShortcutBinding{MOD_CONTROL, 0};
  EXPECT_FALSE(validateUserSettings(settings).m_copy_shortcut_valid);

  settings = defaultUserSettings();
  settings.m_selection_shortcuts.m_toggle_longshot =
      ShortcutBinding{MOD_ALT, VK_TAB};
  EXPECT_FALSE(validateUserSettings(settings).m_toggle_longshot_shortcut_valid);

  settings = defaultUserSettings();
  settings.m_selection_shortcuts.m_copy =
      ShortcutBinding{MOD_CONTROL | MOD_NOREPEAT, static_cast<UINT>('C')};
  EXPECT_FALSE(validateUserSettings(settings).m_copy_shortcut_valid);

  settings = defaultUserSettings();
  settings.m_selection_shortcuts.m_toggle_longshot =
      ShortcutBinding{0, VK_PROCESSKEY};
  EXPECT_FALSE(validateUserSettings(settings).m_toggle_longshot_shortcut_valid);

  settings = defaultUserSettings();
  settings.m_capture_hotkey =
      ShortcutBinding{MOD_WIN, static_cast<UINT>('Q')};
  EXPECT_FALSE(validateUserSettings(settings).m_capture_hotkey_valid);
}

TEST(UserSettingsTest, AllowsEmptySelectionShortcutButRejectsDuplicateActions)
{
  UserSettings settings = defaultUserSettings();

  settings.m_selection_shortcuts.m_copy = ShortcutBinding{};
  EXPECT_TRUE(validateUserSettings(settings).m_copy_shortcut_valid);

  settings = defaultUserSettings();
  settings.m_selection_shortcuts.m_toggle_longshot =
      settings.m_selection_shortcuts.m_copy;
  EXPECT_FALSE(validateUserSettings(settings).m_shortcuts_unique);
}

}  // namespace
}  // namespace qingying
