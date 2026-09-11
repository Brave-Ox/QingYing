#include <cstddef>
#include <stdexcept>
#include <utility>

#include <Windows.h>

#include "qingying/window/smart_region_mode.hpp"

namespace qingying {
namespace {

constexpr wchar_t SmartRegionModeValueName[] = L"SmartRegionMode";
constexpr std::size_t MaximumTestNamespaceLength = 64;

class RegistryKey final
{
 public:
  explicit RegistryKey(HKEY key) noexcept : m_key(key)
  {
  }

  ~RegistryKey()
  {
    if (m_key != nullptr)
    {
      static_cast<void>(RegCloseKey(m_key));
    }
  }

  RegistryKey(const RegistryKey&) = delete;
  RegistryKey& operator=(const RegistryKey&) = delete;

  HKEY get() const noexcept
  {
    return m_key;
  }

 private:
  HKEY m_key{nullptr};
};

bool validTestNamespace(const std::wstring& value) noexcept
{
  return value.size() <= MaximumTestNamespaceLength &&
         value.find_first_not_of(
             L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") ==
             std::wstring::npos;
}

}  // namespace

SmartRegionMode smartRegionModeFromPersistedValue(
    std::uint32_t value) noexcept
{
  switch (value)
  {
    case static_cast<std::uint32_t>(SmartRegionMode::WindowOnly):
      return SmartRegionMode::WindowOnly;
    case static_cast<std::uint32_t>(SmartRegionMode::Disabled):
      return SmartRegionMode::Disabled;
    case static_cast<std::uint32_t>(SmartRegionMode::DetectElements):
    default:
      return SmartRegionMode::DetectElements;
  }
}

bool smartRegionModeForHotkeyId(int hotkey_id,
                                SmartRegionMode& mode) noexcept
{
  switch (hotkey_id)
  {
    case SmartRegionDetectElementsModeHotkeyId:
    case SmartRegionDetectElementsAlternateHotkeyId:
      mode = SmartRegionMode::DetectElements;
      return true;
    case SmartRegionWindowOnlyModeHotkeyId:
    case SmartRegionWindowOnlyAlternateHotkeyId:
      mode = SmartRegionMode::WindowOnly;
      return true;
    case SmartRegionDisabledModeHotkeyId:
    case SmartRegionDisabledAlternateHotkeyId:
      mode = SmartRegionMode::Disabled;
      return true;
    default:
      return false;
  }
}

const wchar_t* smartRegionModeName(SmartRegionMode mode) noexcept
{
  switch (mode)
  {
    case SmartRegionMode::DetectElements:
      return L"detect-elements";
    case SmartRegionMode::WindowOnly:
      return L"window-only";
    case SmartRegionMode::Disabled:
      return L"disabled";
  }
  return L"unknown";
}

SmartRegionModeSettings::SmartRegionModeSettings(std::wstring test_namespace)
    : m_key(L"Software\\QingYing\\Capture")
{
  if (!validTestNamespace(test_namespace))
  {
    throw std::invalid_argument("smart region mode test namespace");
  }
  if (!test_namespace.empty())
  {
    m_key += L"\\Tests\\" + std::move(test_namespace);
  }
}

SmartRegionMode SmartRegionModeSettings::load() const noexcept
{
  DWORD value = 0;
  DWORD bytes = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, m_key.c_str(),
                   SmartRegionModeValueName, RRF_RT_REG_DWORD, nullptr,
                   &value, &bytes) != ERROR_SUCCESS)
  {
    return SmartRegionMode::DetectElements;
  }
  return smartRegionModeFromPersistedValue(value);
}

bool SmartRegionModeSettings::save(SmartRegionMode mode) const noexcept
{
  HKEY raw_key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, m_key.c_str(), 0, nullptr, 0,
                      KEY_SET_VALUE, nullptr, &raw_key, nullptr) !=
      ERROR_SUCCESS)
  {
    return false;
  }
  const RegistryKey key(raw_key);
  const DWORD value = static_cast<DWORD>(mode);
  return RegSetValueExW(
             key.get(), SmartRegionModeValueName, 0, REG_DWORD,
             reinterpret_cast<const BYTE*>(&value), sizeof(value)) ==
         ERROR_SUCCESS;
}

const std::wstring& SmartRegionModeSettings::key() const noexcept
{
  return m_key;
}

}  // namespace qingying
