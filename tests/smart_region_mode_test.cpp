#include <string>

#include <Windows.h>

#include <gtest/gtest.h>

#include "qingying/window/smart_region_mode.hpp"

namespace qingying {
namespace {

TEST(SmartRegionModeTest, UnknownPersistedValueFallsBackToElementDetection)
{
  EXPECT_EQ(smartRegionModeFromPersistedValue(0),
            SmartRegionMode::DetectElements);
  EXPECT_EQ(smartRegionModeFromPersistedValue(1), SmartRegionMode::WindowOnly);
  EXPECT_EQ(smartRegionModeFromPersistedValue(2), SmartRegionMode::Disabled);
  EXPECT_EQ(smartRegionModeFromPersistedValue(99),
            SmartRegionMode::DetectElements);
}

TEST(SmartRegionModeTest, MapsLegacyAndAlternateOverlayHotkeysToModes)
{
  SmartRegionMode mode = SmartRegionMode::Disabled;

  EXPECT_TRUE(
      smartRegionModeForHotkeyId(SmartRegionDetectElementsModeHotkeyId, mode));
  EXPECT_EQ(mode, SmartRegionMode::DetectElements);
  EXPECT_TRUE(smartRegionModeForHotkeyId(
      SmartRegionDetectElementsAlternateHotkeyId, mode));
  EXPECT_EQ(mode, SmartRegionMode::DetectElements);
  EXPECT_TRUE(
      smartRegionModeForHotkeyId(SmartRegionWindowOnlyModeHotkeyId, mode));
  EXPECT_EQ(mode, SmartRegionMode::WindowOnly);
  EXPECT_TRUE(smartRegionModeForHotkeyId(
      SmartRegionWindowOnlyAlternateHotkeyId, mode));
  EXPECT_EQ(mode, SmartRegionMode::WindowOnly);
  EXPECT_TRUE(
      smartRegionModeForHotkeyId(SmartRegionDisabledModeHotkeyId, mode));
  EXPECT_EQ(mode, SmartRegionMode::Disabled);
  EXPECT_TRUE(smartRegionModeForHotkeyId(
      SmartRegionDisabledAlternateHotkeyId, mode));
  EXPECT_EQ(mode, SmartRegionMode::Disabled);
  EXPECT_FALSE(smartRegionModeForHotkeyId(999, mode));
}

TEST(SmartRegionModeTest, NamesModesForRuntimeDiagnostics)
{
  EXPECT_STREQ(smartRegionModeName(SmartRegionMode::DetectElements),
               L"detect-elements");
  EXPECT_STREQ(smartRegionModeName(SmartRegionMode::WindowOnly),
               L"window-only");
  EXPECT_STREQ(smartRegionModeName(SmartRegionMode::Disabled), L"disabled");
}

TEST(SmartRegionModeSettingsTest, DefaultsAndPersistsInAnIsolatedKey)
{
  const std::wstring test_namespace =
      L"mode_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
      std::to_wstring(GetTickCount64());
  SmartRegionModeSettings settings(test_namespace);
  static_cast<void>(RegDeleteKeyW(HKEY_CURRENT_USER, settings.key().c_str()));

  EXPECT_EQ(settings.load(), SmartRegionMode::DetectElements);
  ASSERT_TRUE(settings.save(SmartRegionMode::WindowOnly));
  EXPECT_EQ(SmartRegionModeSettings(test_namespace).load(),
            SmartRegionMode::WindowOnly);
  ASSERT_TRUE(settings.save(SmartRegionMode::Disabled));
  EXPECT_EQ(settings.load(), SmartRegionMode::Disabled);

  EXPECT_EQ(RegDeleteKeyW(HKEY_CURRENT_USER, settings.key().c_str()),
            ERROR_SUCCESS);
}

}  // namespace
}  // namespace qingying
