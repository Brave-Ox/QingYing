#include <array>
#include <cstdint>
#include <cwchar>

#include "known_content_locator.hpp"

namespace qingying::window_detail {
namespace {

constexpr int kClassCapacity = 128;
constexpr int kMinimumChromiumExtentPx = 200;
constexpr DWORD kProcessPathCapacity = 32768;
constexpr std::int64_t kMaximumBrowserChromeHeightNumerator = 1;
constexpr std::int64_t kMaximumBrowserChromeHeightDenominator = 3;

class ScopedHandle {
 public:
  explicit ScopedHandle(HANDLE handle) noexcept : m_handle(handle) {}
  ~ScopedHandle()
  {
    if (m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE) {
      CloseHandle(m_handle);
    }
  }
  ScopedHandle(const ScopedHandle&) = delete;
  ScopedHandle& operator=(const ScopedHandle&) = delete;
  HANDLE get() const noexcept
  {
    return m_handle;
  }

 private:
  HANDLE m_handle{nullptr};
};

bool classNameEquals(HWND window, const wchar_t* expected) noexcept
{
  if (window == nullptr || expected == nullptr) {
    return false;
  }
  wchar_t name[kClassCapacity] = {};
  return GetClassNameW(window, name, kClassCapacity) > 0 &&
         _wcsicmp(name, expected) == 0;
}

bool classNameStartsWith(HWND window, const wchar_t* prefix) noexcept
{
  if (window == nullptr || prefix == nullptr) {
    return false;
  }
  wchar_t name[kClassCapacity] = {};
  const int length = GetClassNameW(window, name, kClassCapacity);
  const std::size_t prefix_length = std::wcslen(prefix);
  return length >= static_cast<int>(prefix_length) &&
         _wcsnicmp(name, prefix, prefix_length) == 0;
}

bool getClientScreenRect(HWND window, RECT& out) noexcept
{
  RECT client{};
  if (window == nullptr || !GetClientRect(window, &client)) {
    return false;
  }
  POINT top_left{client.left, client.top};
  POINT bottom_right{client.right, client.bottom};
  if (!ClientToScreen(window, &top_left) ||
      !ClientToScreen(window, &bottom_right) ||
      bottom_right.x <= top_left.x || bottom_right.y <= top_left.y) {
    return false;
  }
  out = {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
  return true;
}

bool pointInRect(const POINT& point, const RECT& rect) noexcept
{
  return point.x >= rect.left && point.x < rect.right && point.y >= rect.top &&
         point.y < rect.bottom;
}

std::int64_t rectArea(const RECT& rect) noexcept
{
  return (static_cast<std::int64_t>(rect.right) - rect.left) *
         (static_cast<std::int64_t>(rect.bottom) - rect.top);
}

bool setCandidate(HWND root, HWND target, const RECT& rect,
                  SmartRegionCandidate& out) noexcept
{
  out = SmartRegionCandidate{};
  const SmartRegionCandidate candidate{
      reinterpret_cast<std::uintptr_t>(root),
      reinterpret_cast<std::uintptr_t>(target),
      {rect.left, rect.top, rect.right, rect.bottom},
      SmartRegionKind::KnownContent};
  if (!candidate.valid()) {
    return false;
  }
  out = candidate;
  return true;
}

bool executableHasName(HWND window, const wchar_t* expected) noexcept
{
  DWORD process_id = 0;
  GetWindowThreadProcessId(window, &process_id);
  if (process_id == 0 || expected == nullptr) {
    return false;
  }
  const ScopedHandle process(
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id));
  if (process.get() == nullptr) {
    return false;
  }
  std::array<wchar_t, kProcessPathCapacity> path{};
  DWORD path_size = static_cast<DWORD>(path.size());
  if (!QueryFullProcessImageNameW(process.get(), 0, path.data(),
                                  &path_size) ||
      path_size == 0) {
    return false;
  }
  const wchar_t* base_name = path.data();
  for (DWORD index = 0; index < path_size; ++index) {
    if (path[index] == L'\\' || path[index] == L'/') {
      base_name = path.data() + index + 1;
    }
  }
  return _wcsicmp(base_name, expected) == 0;
}

enum class ChromiumRootKind : std::uint8_t {
  Other,
  Browser,
  ElectronWorkbench,
};

struct ChromiumRootClassificationCache {
  HWND m_root{nullptr};
  DWORD m_process_id{0};
  ChromiumRootKind m_kind{ChromiumRootKind::Other};
};

thread_local ChromiumRootClassificationCache g_chromium_root_cache;

ChromiumRootKind classifyChromiumProcess(DWORD process_id) noexcept
{
  if (process_id == 0)
  {
    return ChromiumRootKind::Other;
  }

  const ScopedHandle process(
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id));
  if (process.get() == nullptr)
  {
    return ChromiumRootKind::Other;
  }
  std::array<wchar_t, kProcessPathCapacity> path{};
  DWORD path_size = static_cast<DWORD>(path.size());
  if (!QueryFullProcessImageNameW(process.get(), 0, path.data(),
                                  &path_size) ||
      path_size == 0)
  {
    return ChromiumRootKind::Other;
  }

  const wchar_t* base_name = path.data();
  for (DWORD index = 0; index < path_size; ++index)
  {
    if (path[index] == L'\\' || path[index] == L'/')
    {
      base_name = path.data() + index + 1;
    }
  }
  if (_wcsicmp(base_name, L"chrome.exe") == 0 ||
      _wcsicmp(base_name, L"msedge.exe") == 0 ||
      _wcsicmp(base_name, L"brave.exe") == 0)
  {
    return ChromiumRootKind::Browser;
  }
  if (_wcsicmp(base_name, L"code.exe") == 0 ||
      _wcsicmp(base_name, L"code-insiders.exe") == 0 ||
      _wcsicmp(base_name, L"cursor.exe") == 0 ||
      _wcsicmp(base_name, L"codium.exe") == 0)
  {
    return ChromiumRootKind::ElectronWorkbench;
  }
  return ChromiumRootKind::Other;
}

ChromiumRootKind chromiumRootKind(HWND root) noexcept
{
  if (!classNameEquals(root, L"Chrome_WidgetWin_1"))
  {
    return ChromiumRootKind::Other;
  }

  DWORD process_id = 0;
  GetWindowThreadProcessId(root, &process_id);
  if (g_chromium_root_cache.m_root == root &&
      g_chromium_root_cache.m_process_id == process_id)
  {
    return g_chromium_root_cache.m_kind;
  }

  g_chromium_root_cache.m_root = root;
  g_chromium_root_cache.m_process_id = process_id;
  g_chromium_root_cache.m_kind = classifyChromiumProcess(process_id);
  return g_chromium_root_cache.m_kind;
}

struct ChildCandidate {
  POINT point{};
  HWND window{nullptr};
  RECT rect{};
  std::int64_t area{0};
};

void considerCandidate(HWND window, ChildCandidate& candidate,
                       bool (*matches)(HWND) noexcept,
                       int minimum_extent_px, bool prefer_largest)
{
  RECT rect{};
  if (!IsWindowVisible(window) || !matches(window) ||
      !getClientScreenRect(window, rect) || !pointInRect(candidate.point, rect)) {
    return;
  }
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  if (width < minimum_extent_px || height < minimum_extent_px) {
    return;
  }
  const std::int64_t area = rectArea(rect);
  if (candidate.window == nullptr ||
      (prefer_largest ? area > candidate.area : area < candidate.area)) {
    candidate.window = window;
    candidate.rect = rect;
    candidate.area = area;
  }
}

struct ClassSearch {
  ChildCandidate candidate;
  const wchar_t* expected_class{nullptr};
  int minimum_extent_px{0};
  bool requires_point_match{true};
};

BOOL CALLBACK findLargestClassChild(HWND window, LPARAM parameter)
{
  auto* search = reinterpret_cast<ClassSearch*>(parameter);
  if (search != nullptr) {
    RECT rect{};
    if (IsWindowVisible(window) &&
        classNameEquals(window, search->expected_class) &&
        getClientScreenRect(window, rect) &&
        (!search->requires_point_match ||
         pointInRect(search->candidate.point, rect)) &&
        rect.right - rect.left >= search->minimum_extent_px &&
        rect.bottom - rect.top >= search->minimum_extent_px) {
      const std::int64_t area = rectArea(rect);
      if (search->candidate.window == nullptr || area > search->candidate.area) {
        search->candidate.window = window;
        search->candidate.rect = rect;
        search->candidate.area = area;
      }
    }
  }
  return TRUE;
}

bool locateChromium(HWND root, POINT point, SmartRegionCandidate& out,
                    WindowRect* chromium_browser_chrome,
                    bool* require_complete_visual_boundaries) noexcept
{
  if (chromium_browser_chrome != nullptr)
  {
    *chromium_browser_chrome = WindowRect{};
  }
  if (require_complete_visual_boundaries != nullptr)
  {
    *require_complete_visual_boundaries = false;
  }
  if (!classNameEquals(root, L"Chrome_WidgetWin_1")) {
    return false;
  }
  const ChromiumRootKind root_kind = chromiumRootKind(root);
  if (require_complete_visual_boundaries != nullptr)
  {
    *require_complete_visual_boundaries =
        root_kind == ChromiumRootKind::ElectronWorkbench;
  }
  ClassSearch search;
  search.candidate.point = point;
  search.expected_class = L"Chrome_RenderWidgetHostHWND";
  search.minimum_extent_px = kMinimumChromiumExtentPx;
  search.requires_point_match = false;
  EnumChildWindows(root, findLargestClassChild,
                   reinterpret_cast<LPARAM>(&search));
  if (search.candidate.window == nullptr) {
    search.expected_class = L"Intermediate D3D Window";
    EnumChildWindows(root, findLargestClassChild,
                     reinterpret_cast<LPARAM>(&search));
  }
  if (search.candidate.window == nullptr)
  {
    return false;
  }

  RECT root_client{};
  if (!getClientScreenRect(root, root_client))
  {
    return false;
  }
  const WindowRect root_client_rect{root_client.left, root_client.top,
                                    root_client.right, root_client.bottom};
  const WindowRect renderer_rect{search.candidate.rect.left,
                                 search.candidate.rect.top,
                                 search.candidate.rect.right,
                                 search.candidate.rect.bottom};
  WindowRect chrome_rect;
  if (root_kind == ChromiumRootKind::Browser &&
      chromiumBrowserChromeRect(root_client_rect, renderer_rect, point,
                                chrome_rect))
  {
    if (chromium_browser_chrome != nullptr)
    {
      *chromium_browser_chrome = chrome_rect;
    }
    return false;
  }
  if (!pointInRect(point, search.candidate.rect))
  {
    return false;
  }
  return setCandidate(root, search.candidate.window, search.candidate.rect,
                      out);
}

bool isNotepadEditor(HWND window) noexcept
{
  return (classNameEquals(window, L"Edit") &&
          (GetWindowLongPtrW(window, GWL_STYLE) & ES_MULTILINE) != 0) ||
         classNameStartsWith(window, L"RichEdit");
}

BOOL CALLBACK findLargestNotepadEditor(HWND window, LPARAM parameter)
{
  auto* candidate = reinterpret_cast<ChildCandidate*>(parameter);
  if (candidate != nullptr) {
    considerCandidate(window, *candidate, isNotepadEditor, 0, true);
  }
  return TRUE;
}

bool locateNotepad(HWND root, POINT point, SmartRegionCandidate& out) noexcept
{
  if (!classNameEquals(root, L"Notepad") &&
      !executableHasName(root, L"notepad.exe")) {
    return false;
  }
  ChildCandidate candidate;
  candidate.point = point;
  EnumChildWindows(root, findLargestNotepadEditor,
                   reinterpret_cast<LPARAM>(&candidate));
  return setCandidate(root, candidate.window, candidate.rect, out);
}

bool isExplorerRoot(HWND window) noexcept
{
  if (classNameEquals(window, L"Progman") || classNameEquals(window, L"WorkerW") ||
      classNameEquals(window, L"Shell_TrayWnd") || classNameEquals(window, L"#32769")) {
    return false;
  }
  return classNameEquals(window, L"CabinetWClass") ||
         classNameEquals(window, L"ExploreWClass") ||
         executableHasName(window, L"explorer.exe");
}

bool hasVerticalScrollSurface(HWND window) noexcept
{
  SCROLLBARINFO scrollbar{};
  scrollbar.cbSize = sizeof(scrollbar);
  return GetScrollBarInfo(window, OBJID_VSCROLL, &scrollbar) != FALSE &&
         (scrollbar.rgstate[0] & STATE_SYSTEM_INVISIBLE) == 0;
}

bool isExplorerSurface(HWND window) noexcept
{
  return classNameEquals(window, L"DirectUIHWND") ||
         classNameEquals(window, L"SysListView32") ||
         classNameEquals(window, L"SysTreeView32") ||
         classNameEquals(window, L"SHELLDLL_DefView") ||
         classNameEquals(window, L"DUIViewWndClassName") ||
         classNameEquals(window, L"UIItemsView") ||
         classNameEquals(window, L"ItemsView") ||
         classNameEquals(window, L"ListView") ||
         classNameEquals(window, L"ScrollViewer") ||
         classNameEquals(window, L"Windows.UI.Input.InputSite.WindowClass") ||
         classNameEquals(window, L"Windows.UI.Composition.DesktopWindowContentBridge") ||
         classNameEquals(window, L"Microsoft.UI.Content.DesktopChildSiteBridge") ||
         (GetWindowLongPtrW(window, GWL_STYLE) & WS_VSCROLL) != 0 ||
         hasVerticalScrollSurface(window);
}

BOOL CALLBACK findSmallestExplorerSurface(HWND window, LPARAM parameter)
{
  auto* candidate = reinterpret_cast<ChildCandidate*>(parameter);
  if (candidate != nullptr) {
    considerCandidate(window, *candidate, isExplorerSurface, 0, false);
  }
  return TRUE;
}

bool locateExplorer(HWND root, POINT point, SmartRegionCandidate& out) noexcept
{
  if (!isExplorerRoot(root)) {
    return false;
  }
  HWND current = root;
  for (;;) {
    POINT client_point = point;
    if (!ScreenToClient(current, &client_point)) {
      return false;
    }
    const HWND child = ChildWindowFromPointEx(
        current, client_point,
        CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
    if (child == nullptr || child == current) {
      break;
    }
    current = child;
  }
  while (current != nullptr && current != root) {
    RECT rect{};
    if (isExplorerSurface(current) && getClientScreenRect(current, rect) &&
        pointInRect(point, rect)) {
      return setCandidate(root, current, rect, out);
    }
    current = GetParent(current);
  }
  ChildCandidate candidate;
  candidate.point = point;
  EnumChildWindows(root, findSmallestExplorerSurface,
                   reinterpret_cast<LPARAM>(&candidate));
  return setCandidate(root, candidate.window, candidate.rect, out);
}

}  // namespace

bool chromiumBrowserChromeRect(const WindowRect& root_client_rect,
                               const WindowRect& renderer_rect,
                               POINT screen_point,
                               WindowRect& out) noexcept
{
  out = WindowRect{};
  const std::int64_t chrome_height =
      static_cast<std::int64_t>(renderer_rect.top) - root_client_rect.top;
  const std::int64_t root_height = root_client_rect.height();
  if (root_client_rect.empty() || renderer_rect.empty() ||
      renderer_rect.left != root_client_rect.left ||
      renderer_rect.right != root_client_rect.right ||
      renderer_rect.top <= root_client_rect.top ||
      renderer_rect.top >= root_client_rect.bottom ||
      renderer_rect.bottom > root_client_rect.bottom ||
      chrome_height * kMaximumBrowserChromeHeightDenominator >
          root_height * kMaximumBrowserChromeHeightNumerator)
  {
    return false;
  }

  const WindowRect chrome_rect{root_client_rect.left, root_client_rect.top,
                               root_client_rect.right, renderer_rect.top};
  if (screen_point.x < chrome_rect.left ||
      screen_point.x >= chrome_rect.right ||
      screen_point.y < chrome_rect.top ||
      screen_point.y >= chrome_rect.bottom)
  {
    return false;
  }
  out = chrome_rect;
  return true;
}

bool locateKnownContent(HWND root_window, POINT screen_point,
                        SmartRegionCandidate& out,
                        WindowRect* chromium_browser_chrome,
                        bool* require_complete_visual_boundaries) noexcept
{
  out = SmartRegionCandidate{};
  if (chromium_browser_chrome != nullptr)
  {
    *chromium_browser_chrome = WindowRect{};
  }
  if (require_complete_visual_boundaries != nullptr)
  {
    *require_complete_visual_boundaries = false;
  }
  if (root_window == nullptr || !IsWindow(root_window) ||
      !IsWindowVisible(root_window) || IsIconic(root_window) ||
      GetAncestor(root_window, GA_ROOT) != root_window) {
    return false;
  }
  if (classNameEquals(root_window, L"Chrome_WidgetWin_1"))
  {
    return locateChromium(root_window, screen_point, out,
                          chromium_browser_chrome,
                          require_complete_visual_boundaries);
  }
  return locateNotepad(root_window, screen_point, out) ||
         locateExplorer(root_window, screen_point, out);
}

}  // namespace qingying::window_detail
