#include "qingying/app/autostart_settings.hpp"

#include <Windows.h>

#include <string>

namespace qingying {
namespace {

constexpr wchar_t kRunKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"QingYing";

std::wstring exePath() {
  wchar_t buf[MAX_PATH] = {};
  const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) {
    return {};
  }
  return std::wstring(buf, n);
}

}  // namespace

bool AutostartSettings::isEnabled() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) !=
      ERROR_SUCCESS) {
    return false;
  }

  wchar_t value[MAX_PATH + 4] = {};
  DWORD type = 0;
  DWORD bytes = sizeof(value);
  const LONG rc =
      RegQueryValueExW(key, kValueName, nullptr, &type,
                       reinterpret_cast<LPBYTE>(value), &bytes);
  RegCloseKey(key);

  if (rc != ERROR_SUCCESS || type != REG_SZ) {
    return false;
  }

  const std::wstring current = exePath();
  if (current.empty()) {
    return false;
  }

  std::wstring stored = value;
  if (stored.size() >= 2 && stored.front() == L'"' && stored.back() == L'"') {
    stored = stored.substr(1, stored.size() - 2);
  }
  return _wcsicmp(stored.c_str(), current.c_str()) == 0;
}

bool AutostartSettings::setEnabled(bool enabled) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) !=
      ERROR_SUCCESS) {
    return false;
  }

  LONG rc = ERROR_SUCCESS;
  if (enabled) {
    const std::wstring path = exePath();
    if (path.empty()) {
      RegCloseKey(key);
      return false;
    }
    std::wstring quoted = L"\"";
    quoted += path;
    quoted += L'"';
    rc = RegSetValueExW(
        key, kValueName, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(quoted.c_str()),
        static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t)));
  } else {
    rc = RegDeleteValueW(key, kValueName);
    if (rc == ERROR_FILE_NOT_FOUND) {
      rc = ERROR_SUCCESS;
    }
  }

  RegCloseKey(key);
  return rc == ERROR_SUCCESS;
}

}  // namespace qingying
