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
