#pragma once

#include "qingying/longshot/longshot_engine.hpp"
#include "qingying/ui/shortcut_types.hpp"

#include <cstdint>

namespace qingying {

inline constexpr std::uint32_t UserSettingsSchemaVersion = 1;
inline constexpr int MinimumLongShotFrames = 2;
inline constexpr int MaximumLongShotFrames = 60;
inline constexpr int MinimumLongShotOutputHeight = 3000;
inline constexpr int MaximumLongShotOutputHeight = 60000;

struct UserSettings
{
  std::uint32_t m_schema_version{UserSettingsSchemaVersion};
  ShortcutBinding m_capture_hotkey;
  SelectionShortcutSettings m_selection_shortcuts;
  LongShotLimits m_longshot_limits;
};

struct UserSettingsValidation
{
  bool m_capture_hotkey_valid{false};
  bool m_copy_shortcut_valid{false};
  bool m_toggle_longshot_shortcut_valid{false};
  bool m_shortcuts_unique{false};
  bool m_longshot_limits_valid{false};

  constexpr bool valid() const noexcept
  {
    return m_capture_hotkey_valid && m_copy_shortcut_valid &&
           m_toggle_longshot_shortcut_valid && m_shortcuts_unique &&
           m_longshot_limits_valid;
  }
};

inline UserSettings defaultUserSettings() noexcept
{
  UserSettings settings;
  settings.m_capture_hotkey =
      ShortcutBinding{MOD_CONTROL | MOD_SHIFT, static_cast<UINT>('Q')};
  settings.m_selection_shortcuts = defaultSelectionShortcutSettings();
  settings.m_longshot_limits = LongShotLimits{30, 30000};
  return settings;
}

constexpr bool captureHotkeyIsValid(const ShortcutBinding& binding) noexcept
{
  return !binding.empty() && binding.m_modifiers != 0 && binding.valid() &&
         !shortcutIsReserved(binding);
}

constexpr bool selectionShortcutIsValid(const ShortcutBinding& binding) noexcept
{
  return binding.valid() && !shortcutIsReserved(binding);
}

constexpr bool shortcutsAreUnique(const UserSettings& settings) noexcept
{
  const ShortcutBinding& capture = settings.m_capture_hotkey;
  const ShortcutBinding& copy = settings.m_selection_shortcuts.m_copy;
  const ShortcutBinding& toggle =
      settings.m_selection_shortcuts.m_toggle_longshot;

  if (!copy.empty() && capture == copy)
  {
    return false;
  }
  if (!toggle.empty() && capture == toggle)
  {
    return false;
  }
  return copy.empty() || toggle.empty() || copy != toggle;
}

constexpr bool longShotLimitsAreValid(const LongShotLimits& limits) noexcept
{
  return limits.max_frames >= MinimumLongShotFrames &&
         limits.max_frames <= MaximumLongShotFrames &&
         limits.max_output_height >= MinimumLongShotOutputHeight &&
         limits.max_output_height <= MaximumLongShotOutputHeight;
}

constexpr UserSettingsValidation validateUserSettings(
    const UserSettings& settings) noexcept
{
  return UserSettingsValidation{
      captureHotkeyIsValid(settings.m_capture_hotkey),
      selectionShortcutIsValid(settings.m_selection_shortcuts.m_copy),
      selectionShortcutIsValid(
          settings.m_selection_shortcuts.m_toggle_longshot),
      shortcutsAreUnique(settings),
      longShotLimitsAreValid(settings.m_longshot_limits)};
}

}  // namespace qingying
