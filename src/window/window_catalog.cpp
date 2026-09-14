#include "qingying/window/window_resolver.h"

#include "window_query_helpers.h"

#include <Windows.h>
#include <dwmapi.h>

#include <array>
#include <cwchar>
#include <string_view>
#include <utility>
#include <vector>

namespace qingying::window_detail {
namespace {
constexpr DWORD BrowserProcessPathCapacity = 32768;

class ScopedProcessHandle
{
 public:
  explicit ScopedProcessHandle(HANDLE handle) noexcept : m_handle(handle)
  {
  }

  ~ScopedProcessHandle()
  {
    if (m_handle != nullptr)
    {
      static_cast<void>(CloseHandle(m_handle));
    }
  }

  ScopedProcessHandle(const ScopedProcessHandle&) = delete;
  ScopedProcessHandle& operator=(const ScopedProcessHandle&) = delete;

  HANDLE get() const noexcept
  {
    return m_handle;
  }

 private:
  HANDLE m_handle{nullptr};
};

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

bool isSupportedBrowserExecutableName(
    std::wstring_view executable_path) noexcept
{
  const std::size_t separator = executable_path.find_last_of(L"\\/");
  const std::wstring_view executable_name =
      separator == std::wstring_view::npos
          ? executable_path
          : executable_path.substr(separator + 1);
  return executable_name == L"chrome.exe" || executable_name == L"msedge.exe" ||
         executable_name == L"brave.exe";
}

bool isSupportedBrowserWindow(HWND hwnd) noexcept
{
  const std::uint32_t process_id = processId(hwnd);
  if (process_id == 0)
  {
    return false;
  }
  ScopedProcessHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                          FALSE, process_id));
  if (process.get() == nullptr)
  {
    return false;
  }
  std::array<wchar_t, BrowserProcessPathCapacity> executable_path{};
  DWORD executable_path_length =
      static_cast<DWORD>(executable_path.size());
  if (QueryFullProcessImageNameW(process.get(), 0, executable_path.data(),
                                 &executable_path_length) == FALSE ||
      executable_path_length == 0 ||
      executable_path_length >
          static_cast<DWORD>(executable_path.size()))
  {
    return false;
  }
  return isSupportedBrowserExecutableName(
      {executable_path.data(), executable_path_length});
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
bool isBrowserOwnedTransientPopupStyle(LONG_PTR window_style,
                                       bool has_browser_owner) noexcept
{
  return has_browser_owner && (window_style & WS_POPUP) != 0 &&
         (window_style & WS_CHILD) == 0;
}
bool isBrowserOwnedTransientPopup(HWND hwnd) noexcept
{
  if (hwnd == nullptr || !IsWindow(hwnd) || ownProcess(hwnd) ||
      !IsWindowVisible(hwnd) || IsIconic(hwnd) || cloaked(hwnd) ||
      desktopShell(hwnd))
  {
    return false;
  }
  const HWND owner_window = GetWindow(hwnd, GW_OWNER);
  const HWND owner_root = owner_window == nullptr
                              ? nullptr
                              : GetAncestor(owner_window, GA_ROOT);
  if (!isBrowserOwnedTransientPopupStyle(
          GetWindowLongPtrW(hwnd, GWL_STYLE), owner_root != nullptr) ||
      owner_root == hwnd || !commonCandidate(owner_root))
  {
    return false;
  }
  return isSupportedBrowserWindow(owner_root);
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
