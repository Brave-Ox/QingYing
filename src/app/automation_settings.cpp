#include "qingying/app/automation_settings.h"
#include <Windows.h>
#include <ShlObj.h>
#include <stdexcept>
namespace qingying {
AutomationSettings::AutomationSettings(std::wstring test_namespace)
    : key_(L"Software\\QingYing\\Automation") {
  if (!test_namespace.empty()) {
    if (test_namespace.size() > 64 || test_namespace.find_first_not_of(
        L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::wstring::npos)
      throw std::invalid_argument("automation test namespace");
    key_ += L"\\Tests\\" + test_namespace;
    testing_ = true;
  }
}
bool AutomationSettings::enabled() const {
  DWORD value = 0, bytes = sizeof(value);
  return RegGetValueW(HKEY_CURRENT_USER, key_.c_str(), L"Enabled",
      RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS && value == 1;
}
bool AutomationSettings::setEnabled(bool enabled) const {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, key_.c_str(), 0, nullptr, 0,
      KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
  const DWORD value = enabled ? 1 : 0;
  const auto code = RegSetValueExW(key, L"Enabled", 0, REG_DWORD,
      reinterpret_cast<const BYTE*>(&value), sizeof(value));
  RegCloseKey(key);
  return code == ERROR_SUCCESS;
}
std::vector<std::wstring> AutomationSettings::allowedSaveDirectories() const {
  if (testing_) {
    wchar_t temporary[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, temporary);
    return length != 0 && length < MAX_PATH
        ? std::vector<std::wstring>{temporary} : std::vector<std::wstring>{};
  }
  PWSTR pictures = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_Pictures, KF_FLAG_DEFAULT,
                                  nullptr, &pictures)) || !pictures)
    return {};
  std::wstring path(pictures);
  CoTaskMemFree(pictures);
  return {std::move(path)};
}
}  // namespace qingying
