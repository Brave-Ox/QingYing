#pragma once

#include <Windows.h>

namespace qingying {

inline constexpr UINT ShortcutUserModifierMask =
    MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;

constexpr bool shortcutVirtualKeyIsModifier(UINT virtual_key) noexcept
{
  return virtual_key == VK_SHIFT || virtual_key == VK_CONTROL ||
         virtual_key == VK_MENU || virtual_key == VK_LSHIFT ||
         virtual_key == VK_RSHIFT || virtual_key == VK_LCONTROL ||
         virtual_key == VK_RCONTROL || virtual_key == VK_LMENU ||
         virtual_key == VK_RMENU || virtual_key == VK_LWIN ||
         virtual_key == VK_RWIN;
}

constexpr bool shortcutModifiersAreValid(UINT modifiers) noexcept
{
  return (modifiers & ~ShortcutUserModifierMask) == 0;
}

constexpr bool shortcutVirtualKeyIsSupported(UINT virtual_key) noexcept
{
  return virtual_key != 0 && virtual_key <= 0xFFu &&
         !shortcutVirtualKeyIsModifier(virtual_key) &&
         virtual_key != VK_PROCESSKEY && virtual_key != VK_PACKET;
}

struct ShortcutBinding
{
  UINT m_modifiers{0};
  UINT m_virtual_key{0};

  constexpr bool empty() const noexcept
  {
    return m_modifiers == 0 && m_virtual_key == 0;
  }

  constexpr bool valid() const noexcept
  {
    if (empty())
    {
      return true;
    }
    return shortcutModifiersAreValid(m_modifiers) &&
           shortcutVirtualKeyIsSupported(m_virtual_key);
  }

  constexpr bool operator==(const ShortcutBinding& other) const noexcept
  {
    return m_modifiers == other.m_modifiers &&
           m_virtual_key == other.m_virtual_key;
  }

  constexpr bool operator!=(const ShortcutBinding& other) const noexcept
  {
    return !(*this == other);
  }
};

struct SelectionShortcutSettings
{
  ShortcutBinding m_copy;
  ShortcutBinding m_toggle_longshot;
};

constexpr SelectionShortcutSettings defaultSelectionShortcutSettings() noexcept
{
  return SelectionShortcutSettings{
      ShortcutBinding{MOD_CONTROL, static_cast<UINT>('C')},
      ShortcutBinding{0, static_cast<UINT>('L')}};
}

constexpr bool shortcutIsReserved(const ShortcutBinding& binding) noexcept
{
  if (!binding.valid() || binding.empty())
  {
    return false;
  }

  if ((binding.m_modifiers & MOD_WIN) != 0)
  {
    return true;
  }

  if (binding.m_modifiers == MOD_ALT &&
      (binding.m_virtual_key == VK_TAB || binding.m_virtual_key == VK_ESCAPE))
  {
    return true;
  }

  return binding.m_modifiers == (MOD_CONTROL | MOD_ALT) &&
         binding.m_virtual_key == VK_DELETE;
}

}  // namespace qingying
