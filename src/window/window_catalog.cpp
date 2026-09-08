#include "qingying/window/window_resolver.h"

#include "window_query_helpers.h"

#include <Windows.h>
#include <dwmapi.h>

#include <cwchar>
#include <utility>
#include <vector>

namespace qingying::window_detail {
namespace {
bool validRect(const RECT& r) noexcept { return r.right > r.left && r.bottom > r.top; }
RECT desktopRect() noexcept {
  const LONG x = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const LONG y = GetSystemMetrics(SM_YVIRTUALSCREEN);
  return {x, y, x + GetSystemMetrics(SM_CXVIRTUALSCREEN),
          y + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
}
std::wstring fold(const std::wstring& s) {
  const int n = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
      s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr, 0);
  if (n <= 0) return {};
  std::wstring out(static_cast<std::size_t>(n), L'\0');
  return LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, s.data(),
      static_cast<int>(s.size()), out.data(), n, nullptr, nullptr, 0) == n
      ? out : std::wstring{};
}
}  // namespace

std::uint32_t processId(HWND hwnd) noexcept {
  DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid); return pid;
}
bool ownProcess(HWND hwnd) noexcept { return processId(hwnd) == GetCurrentProcessId(); }
bool desktopShell(HWND hwnd) noexcept {
  if (hwnd == GetDesktopWindow()) return true;
  wchar_t cls[64]{}; GetClassNameW(hwnd, cls, 64);
  return std::wcscmp(cls, L"Progman") == 0 || std::wcscmp(cls, L"WorkerW") == 0 ||
      std::wcscmp(cls, L"#32769") == 0;
}
bool cloaked(HWND hwnd) noexcept {
  BOOL value = FALSE;
  return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &value,
      sizeof(value))) && value != FALSE;
}
bool commonCandidate(HWND hwnd) noexcept {
  return hwnd && IsWindow(hwnd) && !ownProcess(hwnd) && IsWindowVisible(hwnd) &&
      !IsIconic(hwnd) && (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) == 0 &&
      !cloaked(hwnd) && !desktopShell(hwnd);
}
bool readVisibleBounds(HWND hwnd, RECT& out, BoundsReader dwm, BoundsReader fallback) {
  RECT r{};
  const bool first = dwm ? dwm(hwnd, r) : SUCCEEDED(DwmGetWindowAttribute(
      hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)));
  if (first && validRect(r)) { out = r; return true; }
  r = {};
  const bool second = fallback ? fallback(hwnd, r) : GetWindowRect(hwnd, &r) != FALSE;
  if (!second || !validRect(r)) return false;
  out = r; return true;
}
bool intersectsVirtualDesktop(const RECT& r) noexcept {
  const auto d = desktopRect();
  return validRect(r) && r.right > d.left && r.bottom > d.top &&
      r.left < d.right && r.top < d.bottom;
}
bool fullyInsideVirtualDesktop(const RECT& r) noexcept {
  const auto d = desktopRect();
  return validRect(r) && r.left >= d.left && r.top >= d.top &&
      r.right <= d.right && r.bottom <= d.bottom;
}
std::wstring windowTitle(HWND hwnd) {
  const int n = GetWindowTextLengthW(hwnd); if (n <= 0) return {};
  std::vector<wchar_t> text(static_cast<std::size_t>(n) + 1);
  const int copied = GetWindowTextW(hwnd, text.data(), static_cast<int>(text.size()));
  return copied > 0 ? std::wstring(text.data(), copied) : std::wstring{};
}
bool invariantTitleMatch(const std::wstring& title, const std::wstring& query,
                         bool exact) noexcept {
  if (query.empty()) return false;
  try { const auto a = fold(title), b = fold(query);
    return exact ? a == b : a.find(b) != std::wstring::npos;
  } catch (...) { return false; }
}
}  // namespace qingying::window_detail

namespace qingying {
namespace {
constexpr std::size_t kCatalogLimit = 256;

BOOL CALLBACK collect(HWND hwnd, LPARAM parameter) {
  auto& snapshot = *reinterpret_cast<WindowCatalogSnapshot*>(parameter);
  if (!window_detail::commonCandidate(hwnd)) return TRUE;
  RECT bounds{};
  if (!window_detail::readVisibleBounds(hwnd, bounds) ||
      !window_detail::fullyInsideVirtualDesktop(bounds)) return TRUE;
  auto title = window_detail::windowTitle(hwnd);
  if (title.empty()) return TRUE;
  if (snapshot.entries.size() >= kCatalogLimit) {
    snapshot.truncated = true;
    return FALSE;
  }
  snapshot.entries.push_back({reinterpret_cast<std::uintptr_t>(hwnd),
      window_detail::processId(hwnd), std::move(title),
      {bounds.left, bounds.top, bounds.right - bounds.left,
       bounds.bottom - bounds.top}});
  return TRUE;
}
}  // namespace

WindowCatalogSnapshot enumerateCaptureWindows() {
  WindowCatalogSnapshot result;
  EnumWindows(collect, reinterpret_cast<LPARAM>(&result));
  return result;
}

}  // namespace qingying
