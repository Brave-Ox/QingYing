#include "msaa_region_locator.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

#include <wrl/client.h>

namespace qingying::window_detail {
namespace {

constexpr std::uint8_t DefaultMaximumMsaaHitTestDepth = 6;
constexpr std::uint8_t BrowserMaximumMsaaHitTestDepth = 10;
constexpr ULONGLONG BrowserMsaaHitTestBudgetMs = 16;
constexpr LONG BrowserMaximumMsaaChildrenPerLevel = 32;
constexpr std::size_t BrowserMaximumMsaaEnumeratedChildren = 96;
constexpr DWORD ProcessPathCapacity = 32768;
constexpr LONG MaximumRootContentInsetPx = 1;
constexpr LONG BrowserTopChromeHeightDivisor = 3;

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

class ScopedComApartment
{
 public:
  ScopedComApartment() noexcept
      : m_result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)),
        m_should_uninitialize(SUCCEEDED(m_result))
  {
  }

  ~ScopedComApartment()
  {
    if (m_should_uninitialize)
    {
      CoUninitialize();
    }
  }

  ScopedComApartment(const ScopedComApartment&) = delete;
  ScopedComApartment& operator=(const ScopedComApartment&) = delete;

  bool usable() const noexcept
  {
    return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE;
  }

 private:
  HRESULT m_result{E_FAIL};
  bool m_should_uninitialize{false};
};

class ScopedVariant
{
 public:
  ScopedVariant() noexcept
  {
    VariantInit(&m_value);
  }

  ~ScopedVariant()
  {
    static_cast<void>(VariantClear(&m_value));
  }

  ScopedVariant(const ScopedVariant&) = delete;
  ScopedVariant& operator=(const ScopedVariant&) = delete;

  VARIANT* address() noexcept
  {
    return &m_value;
  }

  const VARIANT& value() const noexcept
  {
    return m_value;
  }

  void setLong(LONG value) noexcept
  {
    m_value.vt = VT_I4;
    m_value.lVal = value;
  }

 private:
 VARIANT m_value{};
};

class ScopedVariantArray
{
 public:
  ScopedVariantArray() noexcept
  {
    for (VARIANT& value : m_values)
    {
      VariantInit(&value);
    }
  }

  ~ScopedVariantArray()
  {
    for (VARIANT& value : m_values)
    {
      static_cast<void>(VariantClear(&value));
    }
  }

  ScopedVariantArray(const ScopedVariantArray&) = delete;
  ScopedVariantArray& operator=(const ScopedVariantArray&) = delete;

  VARIANT* data() noexcept
  {
    return m_values.data();
  }

 private:
  std::array<VARIANT, BrowserMaximumMsaaChildrenPerLevel> m_values{};
};

struct BrowserMsaaEnumerationBudget
{
  ULONGLONG start_time_ms{0};
  std::size_t remaining_children{BrowserMaximumMsaaEnumeratedChildren};
  SmartRegionMsaaTraversalDiagnostic* diagnostic{nullptr};

  bool timeBudgetExhausted() const noexcept
  {
    return GetTickCount64() - start_time_ms > BrowserMsaaHitTestBudgetMs;
  }

  bool nodeBudgetExhausted() const noexcept
  {
    return remaining_children == 0;
  }

  bool exhausted() const noexcept
  {
    return nodeBudgetExhausted() || timeBudgetExhausted();
  }

  bool consumeChild() noexcept
  {
    if (exhausted())
    {
      return false;
    }
    --remaining_children;
    if (diagnostic != nullptr)
    {
      ++diagnostic->visited_child_count;
    }
    return true;
  }
};

void setMsaaTraversalStopReason(
    SmartRegionMsaaTraversalDiagnostic* diagnostic,
    SmartRegionMsaaTraversalStopReason stop_reason) noexcept
{
  if (diagnostic != nullptr)
  {
    diagnostic->stop_reason = stop_reason;
  }
}

void setMsaaBudgetExhaustionReason(
    const BrowserMsaaEnumerationBudget& budget,
    SmartRegionMsaaTraversalDiagnostic* diagnostic) noexcept
{
  setMsaaTraversalStopReason(
      diagnostic, budget.timeBudgetExhausted()
                      ? SmartRegionMsaaTraversalStopReason::TimeBudgetExhausted
                      : SmartRegionMsaaTraversalStopReason::NodeBudgetExhausted);
}

bool contains(const WindowRect& rect, POINT point) noexcept
{
  return !rect.empty() && point.x >= rect.left && point.x < rect.right &&
         point.y >= rect.top && point.y < rect.bottom;
}

bool isSupportedBrowserExecutableName(std::wstring_view executable_path) noexcept
{
  const std::size_t separator = executable_path.find_last_of(L"\\/");
  const std::wstring_view executable_name =
      separator == std::wstring_view::npos
          ? executable_path
          : executable_path.substr(separator + 1);
  return executable_name == L"chrome.exe" || executable_name == L"msedge.exe" ||
         executable_name == L"brave.exe";
}

bool isSupportedBrowserWindow(HWND root_window) noexcept
{
  DWORD process_id = 0;
  static_cast<void>(GetWindowThreadProcessId(root_window, &process_id));
  if (process_id == 0)
  {
    return false;
  }

  ScopedProcessHandle process(
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id));
  if (process.get() == nullptr)
  {
    return false;
  }

  std::array<wchar_t, ProcessPathCapacity> executable_path{};
  DWORD executable_path_length =
      static_cast<DWORD>(executable_path.size());
  if (QueryFullProcessImageNameW(process.get(), 0, executable_path.data(),
                                 &executable_path_length) == FALSE ||
      executable_path_length == 0 ||
      executable_path_length > executable_path.size())
  {
    return false;
  }
  return isSupportedBrowserExecutableName(
      {executable_path.data(), executable_path_length});
}

SmartRegionSemantic semanticForRole(LONG role, LONG state) noexcept
{
  switch (role)
  {
    case ROLE_SYSTEM_PUSHBUTTON:
    case ROLE_SYSTEM_CHECKBUTTON:
    case ROLE_SYSTEM_RADIOBUTTON:
    case ROLE_SYSTEM_COMBOBOX:
    case ROLE_SYSTEM_DROPLIST:
    case ROLE_SYSTEM_MENUITEM:
    case ROLE_SYSTEM_BUTTONMENU:
    case ROLE_SYSTEM_PAGETAB:
    case ROLE_SYSTEM_LINK:
    case ROLE_SYSTEM_SLIDER:
    case ROLE_SYSTEM_SPINBUTTON:
    case ROLE_SYSTEM_LISTITEM:
    case ROLE_SYSTEM_OUTLINEITEM:
      return SmartRegionSemantic::ActionableControl;
    case ROLE_SYSTEM_TEXT:
      return (state & STATE_SYSTEM_FOCUSABLE) != 0
                 ? SmartRegionSemantic::ActionableControl
                 : SmartRegionSemantic::Unknown;
    case ROLE_SYSTEM_CLIENT:
    case ROLE_SYSTEM_DOCUMENT:
    case ROLE_SYSTEM_LIST:
    case ROLE_SYSTEM_TABLE:
    case ROLE_SYSTEM_OUTLINE:
    case ROLE_SYSTEM_PANE:
    case ROLE_SYSTEM_TOOLBAR:
    case ROLE_SYSTEM_GROUPING:
    case ROLE_SYSTEM_GRAPHIC:
      return SmartRegionSemantic::ContentSurface;
    default:
      break;
  }
  return SmartRegionSemantic::Unknown;
}

bool shouldReplaceCandidate(const SmartRegionCandidate& candidate,
                            const SmartRegionCandidate& current) noexcept
{
  if (!current.valid())
  {
    return true;
  }
  if (candidate.semantic == SmartRegionSemantic::ActionableControl &&
      current.semantic != SmartRegionSemantic::ActionableControl)
  {
    return true;
  }
  if (candidate.semantic != SmartRegionSemantic::ActionableControl &&
      current.semantic == SmartRegionSemantic::ActionableControl)
  {
    return false;
  }
  const std::int64_t candidate_area =
      static_cast<std::int64_t>(candidate.rect.width()) *
      static_cast<std::int64_t>(candidate.rect.height());
  const std::int64_t current_area =
      static_cast<std::int64_t>(current.rect.width()) *
      static_cast<std::int64_t>(current.rect.height());
  return candidate_area < current_area ||
         (candidate_area == current_area &&
          candidate.accessibility_depth > current.accessibility_depth);
}

bool targetBelongsToRoot(HWND root_window, HWND target_window) noexcept
{
  if (target_window == nullptr || target_window == root_window)
  {
    return true;
  }
  if (!IsWindow(root_window) || !IsWindow(target_window))
  {
    return true;
  }
  return GetAncestor(target_window, GA_ROOT) == root_window;
}

bool isWindowSizedContentSurface(HWND root_window,
                                 const WindowRect& rect) noexcept
{
  if (!IsWindow(root_window))
  {
    return false;
  }
  RECT client_rect{};
  if (GetClientRect(root_window, &client_rect) == FALSE)
  {
    return false;
  }
  POINT top_left{client_rect.left, client_rect.top};
  POINT bottom_right{client_rect.right, client_rect.bottom};
  if (ClientToScreen(root_window, &top_left) == FALSE ||
      ClientToScreen(root_window, &bottom_right) == FALSE)
  {
    return false;
  }
  return msaaRectCoversRootClientArea(
      rect, {top_left.x, top_left.y, bottom_right.x, bottom_right.y});
}

bool isBrowserTopChromePoint(HWND root_window, POINT screen_point) noexcept
{
  RECT client_rect{};
  if (GetClientRect(root_window, &client_rect) == FALSE ||
      client_rect.bottom <= client_rect.top)
  {
    return false;
  }
  POINT top_left{client_rect.left, client_rect.top};
  POINT bottom_right{client_rect.right, client_rect.bottom};
  if (ClientToScreen(root_window, &top_left) == FALSE ||
      ClientToScreen(root_window, &bottom_right) == FALSE)
  {
    return false;
  }
  const LONG height = bottom_right.y - top_left.y;
  if (height <= 0)
  {
    return false;
  }
  const LONG top_chrome_bottom =
      top_left.y + height / BrowserTopChromeHeightDivisor;
  return screen_point.y >= top_left.y &&
         screen_point.y < top_chrome_bottom;
}

bool shouldRecoverBrowserFilteredNode(
    bool is_browser_window, bool is_browser_top_chrome,
    SmartRegionMsaaFilteredNodeReason reason) noexcept
{
  if (!is_browser_window || !is_browser_top_chrome)
  {
    return false;
  }
  return reason == SmartRegionMsaaFilteredNodeReason::UnknownSemantic ||
         reason ==
             SmartRegionMsaaFilteredNodeReason::InvisibleOrOffscreen;
}

void recordLatestFilteredNode(
    SmartRegionMsaaTraversalDiagnostic* diagnostic,
    const SmartRegionMsaaFilteredNodeDiagnostic& filtered_node) noexcept
{
  if (diagnostic != nullptr &&
      filtered_node.reason != SmartRegionMsaaFilteredNodeReason::None)
  {
    diagnostic->filtered_node = filtered_node;
  }
}

void recordFilteredMsaaNode(
    SmartRegionMsaaFilteredNodeDiagnostic* out_filtered_node,
    SmartRegionMsaaFilteredNodeReason reason,
    const MsaaRegionProperties& properties,
    std::uint8_t accessibility_depth) noexcept
{
  if (out_filtered_node == nullptr)
  {
    return;
  }
  out_filtered_node->reason = reason;
  out_filtered_node->rect = properties.rect;
  out_filtered_node->role = properties.role;
  out_filtered_node->state = properties.state;
  out_filtered_node->accessibility_depth = accessibility_depth;
}

SmartRegionMsaaFilteredNodeDiagnostic* filteredNodeDiagnostic(
    SmartRegionMsaaTraversalDiagnostic* diagnostic) noexcept
{
  return diagnostic == nullptr ? nullptr : &diagnostic->filtered_node;
}

bool getAccessibleRect(IAccessible* accessible, const VARIANT& child,
                       WindowRect& out) noexcept
{
  out = WindowRect{};
  if (accessible == nullptr)
  {
    return false;
  }

  LONG left = 0;
  LONG top = 0;
  LONG width = 0;
  LONG height = 0;
  constexpr LONG MaximumCoordinate = (std::numeric_limits<LONG>::max)();
  if (FAILED(accessible->accLocation(&left, &top, &width, &height, child)) ||
      width <= 0 || height <= 0 || left > MaximumCoordinate - width ||
      top > MaximumCoordinate - height)
  {
    return false;
  }
  out = {left, top, left + width, top + height};
  return true;
}

bool makeCandidateFromAccessible(HWND root_window,
                                 IAccessible* accessible,
                                 const VARIANT& child,
                                 POINT screen_point,
                                 std::uint8_t accessibility_depth,
                                 SmartRegionCandidate& out,
                                 MsaaRegionProperties* out_properties = nullptr,
                                 SmartRegionMsaaFilteredNodeDiagnostic*
                                     out_filtered_node = nullptr)
    noexcept
{
  if (accessible == nullptr)
  {
    recordFilteredMsaaNode(
        out_filtered_node, SmartRegionMsaaFilteredNodeReason::MissingAccessible,
        {}, accessibility_depth);
    return false;
  }

  ScopedVariant role;
  ScopedVariant state;
  if (FAILED(accessible->get_accRole(child, role.address())) ||
      role.value().vt != VT_I4)
  {
    recordFilteredMsaaNode(
        out_filtered_node, SmartRegionMsaaFilteredNodeReason::RoleUnavailable,
        {}, accessibility_depth);
    return false;
  }
  static_cast<void>(accessible->get_accState(child, state.address()));

  const LONG state_value = state.value().vt == VT_I4 ? state.value().lVal : 0;
  MsaaRegionProperties properties;
  properties.role = role.value().lVal;
  properties.state = state_value;

  WindowRect accessible_rect;
  if (!getAccessibleRect(accessible, child, accessible_rect))
  {
    recordFilteredMsaaNode(
        out_filtered_node, SmartRegionMsaaFilteredNodeReason::RectUnavailable,
        properties, accessibility_depth);
    return false;
  }

  HWND target_window = nullptr;
  if (FAILED(WindowFromAccessibleObject(accessible, &target_window)))
  {
    target_window = root_window;
  }
  properties.rect = accessible_rect;
  if (out_properties != nullptr)
  {
    *out_properties = properties;
  }
  return makeMsaaCandidate(root_window, target_window, screen_point,
                           properties, out, accessibility_depth,
                           out_filtered_node);
}

void rememberCandidate(const SmartRegionCandidate& candidate,
                       SmartRegionCandidate& best_candidate,
                       bool& has_candidate) noexcept
{
  if (!has_candidate || shouldReplaceCandidate(candidate, best_candidate))
  {
    best_candidate = candidate;
    has_candidate = true;
  }
}

void enumerateContainingMsaaChildren(
    HWND root_window, IAccessible* accessible, POINT screen_point,
    std::uint8_t accessibility_depth, std::uint8_t maximum_depth,
    BrowserMsaaEnumerationBudget& budget, SmartRegionCandidate& best_candidate,
    bool& has_candidate,
    SmartRegionMsaaTraversalDiagnostic* diagnostic) noexcept
{
  if (diagnostic != nullptr)
  {
    diagnostic->path = SmartRegionMsaaTraversalPath::AccessibleChildren;
  }
  if (accessible == nullptr || accessibility_depth >= maximum_depth ||
      budget.exhausted())
  {
    if (accessibility_depth >= maximum_depth)
    {
      setMsaaTraversalStopReason(
          diagnostic, SmartRegionMsaaTraversalStopReason::DepthLimitReached);
    }
    else if (budget.exhausted())
    {
      setMsaaBudgetExhaustionReason(budget, diagnostic);
    }
    return;
  }

  LONG reported_child_count = 0;
  static_cast<void>(accessible->get_accChildCount(&reported_child_count));
  LONG child_start = 0;
  while (!budget.exhausted())
  {
    const LONG remaining_reported_children =
        reported_child_count > child_start ? reported_child_count - child_start
                                           : 0;
    const LONG requested_child_count = msaaAccessibleChildrenRequestCount(
        remaining_reported_children, budget.remaining_children);
    if (requested_child_count <= 0)
    {
      setMsaaBudgetExhaustionReason(budget, diagnostic);
      return;
    }

    ScopedVariantArray children;
    LONG returned_child_count = 0;
    if (FAILED(AccessibleChildren(accessible, child_start,
                                  requested_child_count, children.data(),
                                  &returned_child_count)) ||
        returned_child_count <= 0)
    {
      setMsaaTraversalStopReason(
          diagnostic, SmartRegionMsaaTraversalStopReason::NoChildren);
      return;
    }
    returned_child_count =
        (std::min)(returned_child_count, requested_child_count);
    for (LONG child_index = 0;
         child_index < returned_child_count && !budget.exhausted();
         ++child_index)
    {
      if (!budget.consumeChild())
      {
        setMsaaBudgetExhaustionReason(budget, diagnostic);
        return;
      }

      const VARIANT& child = children.data()[child_index];
      Microsoft::WRL::ComPtr<IAccessible> child_accessible;
      ScopedVariant self;
      const VARIANT* child_identifier = &child;
      IAccessible* child_owner = accessible;
      if (child.vt == VT_I4)
      {
        Microsoft::WRL::ComPtr<IDispatch> child_dispatch;
        if (SUCCEEDED(accessible->get_accChild(child, &child_dispatch)) &&
            child_dispatch != nullptr)
        {
          static_cast<void>(child_dispatch.As(&child_accessible));
        }
      }
      else if (child.vt == VT_DISPATCH && child.pdispVal != nullptr &&
               SUCCEEDED(child.pdispVal->QueryInterface(
                   IID_PPV_ARGS(child_accessible.GetAddressOf()))) &&
               child_accessible != nullptr)
      {
        self.setLong(CHILDID_SELF);
        child_identifier = &self.value();
        child_owner = child_accessible.Get();
      }
      else
      {
        continue;
      }

      WindowRect child_rect;
      if (!getAccessibleRect(child_owner, *child_identifier, child_rect) ||
          !msaaChildContainsScreenPoint(child_rect, screen_point))
      {
        continue;
      }

      const std::uint8_t child_depth =
          static_cast<std::uint8_t>(accessibility_depth + 1);
      SmartRegionCandidate child_candidate;
      if (makeCandidateFromAccessible(root_window, child_owner,
                                      *child_identifier, screen_point,
                                      child_depth, child_candidate, nullptr,
                                      filteredNodeDiagnostic(diagnostic)))
      {
        rememberCandidate(child_candidate, best_candidate, has_candidate);
      }
      if (child_accessible == nullptr || child_accessible.Get() == accessible)
      {
        continue;
      }
      enumerateContainingMsaaChildren(
          root_window, child_accessible.Get(), screen_point, child_depth,
          maximum_depth, budget, best_candidate, has_candidate, diagnostic);
    }

    child_start += returned_child_count;
    if (returned_child_count < requested_child_count ||
        (reported_child_count > 0 && child_start >= reported_child_count))
    {
      setMsaaTraversalStopReason(
          diagnostic, SmartRegionMsaaTraversalStopReason::NoChildren);
      return;
    }
  }
}

bool locateRootScopedMsaaCandidate(HWND root_window, POINT screen_point,
                                   SmartRegionCandidate& out,
                                   SmartRegionMsaaTraversalDiagnostic*
                                       diagnostic,
                                   bool* out_browser_semantic_miss) noexcept
{
  if (out_browser_semantic_miss != nullptr)
  {
    *out_browser_semantic_miss = false;
  }
  if (diagnostic != nullptr)
  {
    *diagnostic = SmartRegionMsaaTraversalDiagnostic{};
    diagnostic->path = SmartRegionMsaaTraversalPath::RootHitTest;
  }
  Microsoft::WRL::ComPtr<IAccessible> root_accessible;
  if (FAILED(AccessibleObjectFromWindow(
          root_window, static_cast<DWORD>(OBJID_CLIENT), IID_IAccessible,
      reinterpret_cast<void**>(root_accessible.GetAddressOf()))) ||
      root_accessible == nullptr)
  {
    setMsaaTraversalStopReason(
        diagnostic, SmartRegionMsaaTraversalStopReason::HitTestFailed);
    return false;
  }

  Microsoft::WRL::ComPtr<IAccessible> current_accessible = root_accessible;
  SmartRegionCandidate best_candidate;
  bool has_candidate = false;
  std::uint8_t accessibility_depth = 0;
  const bool is_browser_window = isSupportedBrowserWindow(root_window);
  const bool is_browser_top_chrome =
      is_browser_window && isBrowserTopChromePoint(root_window, screen_point);
  const std::uint8_t maximum_depth =
      msaaHitTestDepthLimit(is_browser_window);
  const ULONGLONG start_time_ms = GetTickCount64();
  BrowserMsaaEnumerationBudget enumeration_budget{start_time_ms,
                                                  BrowserMaximumMsaaEnumeratedChildren,
                                                  diagnostic};
  while (accessibility_depth < maximum_depth &&
         (!is_browser_window ||
          !enumeration_budget.exhausted()))
  {
    ScopedVariant hit;
    if (FAILED(current_accessible->accHitTest(screen_point.x, screen_point.y,
                                               hit.address())))
    {
      setMsaaTraversalStopReason(
          diagnostic, SmartRegionMsaaTraversalStopReason::HitTestFailed);
      break;
    }
    const std::uint8_t child_depth =
        static_cast<std::uint8_t>(accessibility_depth + 1);
    if (hit.value().vt == VT_I4)
    {
      SmartRegionCandidate hit_candidate;
      SmartRegionMsaaFilteredNodeDiagnostic filtered_node;
      if (makeCandidateFromAccessible(
              root_window, current_accessible.Get(), hit.value(), screen_point,
              child_depth, hit_candidate, nullptr,
              &filtered_node))
      {
        rememberCandidate(hit_candidate, best_candidate, has_candidate);
        if (is_browser_window &&
            msaaShouldEnumerateChildren(
                {hit_candidate.rect,
                 static_cast<LONG>(hit_candidate.accessibility_role), 0}))
        {
          enumerateContainingMsaaChildren(
              root_window, current_accessible.Get(), screen_point, child_depth,
              maximum_depth, enumeration_budget, best_candidate, has_candidate,
              diagnostic);
        }
      }
      else
      {
        recordLatestFilteredNode(diagnostic, filtered_node);
        const bool should_recover = shouldRecoverBrowserFilteredNode(
            is_browser_window, is_browser_top_chrome, filtered_node.reason);
        if (should_recover)
        {
          if (out_browser_semantic_miss != nullptr)
          {
            *out_browser_semantic_miss = true;
          }
          enumerateContainingMsaaChildren(
              root_window, current_accessible.Get(), screen_point, child_depth,
              maximum_depth, enumeration_budget, best_candidate, has_candidate,
              diagnostic);
        }
      }

      Microsoft::WRL::ComPtr<IDispatch> child_dispatch;
      if (FAILED(current_accessible->get_accChild(hit.value(),
                                                   &child_dispatch)) ||
          child_dispatch == nullptr)
      {
        break;
      }
      Microsoft::WRL::ComPtr<IAccessible> child_accessible;
      if (FAILED(child_dispatch.As(&child_accessible)) ||
          child_accessible == nullptr ||
          child_accessible.Get() == current_accessible.Get())
      {
        break;
      }
      current_accessible = child_accessible;
      accessibility_depth = child_depth;
      continue;
    }
    if (hit.value().vt != VT_DISPATCH || hit.value().pdispVal == nullptr)
    {
      break;
    }

    Microsoft::WRL::ComPtr<IAccessible> child_accessible;
    if (FAILED(hit.value().pdispVal->QueryInterface(
            IID_PPV_ARGS(child_accessible.GetAddressOf()))) ||
        child_accessible == nullptr)
    {
      break;
    }
    ScopedVariant self;
    self.setLong(CHILDID_SELF);
    SmartRegionCandidate hit_candidate;
    SmartRegionMsaaFilteredNodeDiagnostic filtered_node;
    if (makeCandidateFromAccessible(root_window, child_accessible.Get(),
                                    self.value(), screen_point, child_depth,
                                    hit_candidate, nullptr,
                                    &filtered_node))
    {
      rememberCandidate(hit_candidate, best_candidate, has_candidate);
      if (is_browser_window &&
          msaaShouldEnumerateChildren(
              {hit_candidate.rect,
               static_cast<LONG>(hit_candidate.accessibility_role), 0}))
      {
        enumerateContainingMsaaChildren(
            root_window, child_accessible.Get(), screen_point, child_depth,
            maximum_depth, enumeration_budget, best_candidate, has_candidate,
            diagnostic);
      }
    }
    else
    {
      recordLatestFilteredNode(diagnostic, filtered_node);
      const bool should_recover = shouldRecoverBrowserFilteredNode(
          is_browser_window, is_browser_top_chrome, filtered_node.reason);
      if (should_recover)
      {
        if (out_browser_semantic_miss != nullptr)
        {
          *out_browser_semantic_miss = true;
        }
        enumerateContainingMsaaChildren(
            root_window, current_accessible.Get(), screen_point, child_depth,
            maximum_depth, enumeration_budget, best_candidate, has_candidate,
            diagnostic);
      }
    }
    if (child_accessible.Get() == current_accessible.Get())
    {
      break;
    }
    current_accessible = child_accessible;
    accessibility_depth = child_depth;
  }

  if (has_candidate)
  {
    setMsaaTraversalStopReason(
        diagnostic, SmartRegionMsaaTraversalStopReason::CandidateFound);
    out = best_candidate;
    return true;
  }
  ScopedVariant self;
  self.setLong(CHILDID_SELF);
  SmartRegionMsaaFilteredNodeDiagnostic filtered_node;
  const bool made_candidate = makeCandidateFromAccessible(
      root_window, current_accessible.Get(), self.value(), screen_point,
      accessibility_depth, out, nullptr, &filtered_node);
  if (!made_candidate)
  {
    recordLatestFilteredNode(diagnostic, filtered_node);
    const bool should_recover = shouldRecoverBrowserFilteredNode(
        is_browser_window, is_browser_top_chrome, filtered_node.reason);
    if (should_recover)
    {
      if (out_browser_semantic_miss != nullptr)
      {
        *out_browser_semantic_miss = true;
      }
      enumerateContainingMsaaChildren(
          root_window, current_accessible.Get(), screen_point,
          accessibility_depth, maximum_depth, enumeration_budget,
          best_candidate, has_candidate, diagnostic);
    }
  }
  if (has_candidate)
  {
    setMsaaTraversalStopReason(
        diagnostic, SmartRegionMsaaTraversalStopReason::CandidateFound);
    out = best_candidate;
    return true;
  }
  setMsaaTraversalStopReason(
      diagnostic, made_candidate ? SmartRegionMsaaTraversalStopReason::CandidateFound
                                  : SmartRegionMsaaTraversalStopReason::NoCandidate);
  return made_candidate;
}

}  // namespace

bool msaaShouldEnumerateChildren(
    const MsaaRegionProperties& properties) noexcept
{
  switch (properties.role)
  {
    case ROLE_SYSTEM_CLIENT:
    case ROLE_SYSTEM_PANE:
    case ROLE_SYSTEM_TOOLBAR:
    case ROLE_SYSTEM_GROUPING:
      return true;
    default:
      return false;
  }
}

bool msaaShouldDeferDirectBrowserContainerCandidate(
    bool is_browser_window,
    const MsaaRegionProperties& properties) noexcept
{
  return is_browser_window && msaaShouldEnumerateChildren(properties);
}

bool msaaChildContainsScreenPoint(const WindowRect& child_rect,
                                  POINT screen_point) noexcept
{
  return contains(child_rect, screen_point);
}

LONG msaaAccessibleChildrenRequestCount(
    LONG reported_child_count, std::size_t remaining_budget) noexcept
{
  if (remaining_budget == 0)
  {
    return 0;
  }
  const LONG requested = reported_child_count > 0
                             ? (std::min)(reported_child_count,
                                          BrowserMaximumMsaaChildrenPerLevel)
                             : BrowserMaximumMsaaChildrenPerLevel;
  const std::size_t bounded_request =
      (std::min)(static_cast<std::size_t>(requested), remaining_budget);
  return static_cast<LONG>(bounded_request);
}

bool msaaShouldRecoverBrowserFilteredNode(
    bool is_browser_window, bool is_browser_top_chrome,
    SmartRegionMsaaFilteredNodeReason reason) noexcept
{
  return shouldRecoverBrowserFilteredNode(
      is_browser_window, is_browser_top_chrome, reason);
}

std::uint8_t msaaHitTestDepthLimit(bool is_browser_window) noexcept
{
  return is_browser_window ? BrowserMaximumMsaaHitTestDepth
                           : DefaultMaximumMsaaHitTestDepth;
}

bool msaaRectCoversRootClientArea(const WindowRect& rect,
                                  const WindowRect& root_client_rect) noexcept
{
  if (rect.empty() || root_client_rect.empty())
  {
    return false;
  }
  const std::int64_t left_limit =
      static_cast<std::int64_t>(root_client_rect.left) +
      MaximumRootContentInsetPx;
  const std::int64_t top_limit =
      static_cast<std::int64_t>(root_client_rect.top) +
      MaximumRootContentInsetPx;
  const std::int64_t right_limit =
      static_cast<std::int64_t>(root_client_rect.right) -
      MaximumRootContentInsetPx;
  const std::int64_t bottom_limit =
      static_cast<std::int64_t>(root_client_rect.bottom) -
      MaximumRootContentInsetPx;
  return static_cast<std::int64_t>(rect.left) <= left_limit &&
         static_cast<std::int64_t>(rect.top) <= top_limit &&
         static_cast<std::int64_t>(rect.right) >= right_limit &&
         static_cast<std::int64_t>(rect.bottom) >= bottom_limit;
}

bool makeMsaaCandidate(HWND root_window, HWND target_window,
                       POINT screen_point,
                       const MsaaRegionProperties& properties,
                       SmartRegionCandidate& out,
                       std::uint8_t accessibility_depth,
                       SmartRegionMsaaFilteredNodeDiagnostic*
                           out_filtered_node) noexcept
{
  out = SmartRegionCandidate{};
  const SmartRegionSemantic semantic =
      semanticForRole(properties.role, properties.state);
  if (root_window == nullptr)
  {
    return false;
  }
  if (semantic == SmartRegionSemantic::Unknown)
  {
    recordFilteredMsaaNode(
        out_filtered_node, SmartRegionMsaaFilteredNodeReason::UnknownSemantic,
        properties, accessibility_depth);
    return false;
  }
  if (!targetBelongsToRoot(root_window, target_window))
  {
    recordFilteredMsaaNode(
        out_filtered_node, SmartRegionMsaaFilteredNodeReason::OutsideOwner,
        properties, accessibility_depth);
    return false;
  }
  if (semantic == SmartRegionSemantic::ContentSurface &&
      isWindowSizedContentSurface(root_window, properties.rect))
  {
    recordFilteredMsaaNode(
        out_filtered_node,
        SmartRegionMsaaFilteredNodeReason::WindowSizedContentSurface,
        properties, accessibility_depth);
    return false;
  }
  if ((properties.state & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN)) !=
      0)
  {
    recordFilteredMsaaNode(
        out_filtered_node,
        SmartRegionMsaaFilteredNodeReason::InvisibleOrOffscreen, properties,
        accessibility_depth);
    return false;
  }
  if (!contains(properties.rect, screen_point))
  {
    recordFilteredMsaaNode(
        out_filtered_node, SmartRegionMsaaFilteredNodeReason::PointerOutside,
        properties, accessibility_depth);
    return false;
  }

  out.owner_window = reinterpret_cast<std::uintptr_t>(root_window);
  out.target_window = reinterpret_cast<std::uintptr_t>(
      target_window == nullptr ? root_window : target_window);
  out.rect = properties.rect;
  out.kind = SmartRegionKind::KnownContent;
  out.source = SmartRegionDiagnosticSource::Msaa;
  out.semantic = semantic;
  out.accessibility_role = static_cast<std::uint32_t>(properties.role);
  out.accessibility_depth = accessibility_depth;
  return true;
}

bool locateMsaaCandidate(HWND root_window, POINT screen_point,
                         SmartRegionCandidate& out,
                         SmartRegionMsaaTraversalDiagnostic*
                             out_diagnostic,
                         bool* out_browser_semantic_miss) noexcept
{
  out = SmartRegionCandidate{};
  if (out_browser_semantic_miss != nullptr)
  {
    *out_browser_semantic_miss = false;
  }
  SmartRegionMsaaTraversalDiagnostic local_diagnostic;
  SmartRegionMsaaTraversalDiagnostic* const diagnostic =
      out_diagnostic != nullptr ? out_diagnostic : &local_diagnostic;
  if (diagnostic != nullptr)
  {
    *diagnostic = SmartRegionMsaaTraversalDiagnostic{};
  }
  if (root_window == nullptr || !IsWindow(root_window) ||
      !IsWindowVisible(root_window))
  {
    return false;
  }

  ScopedComApartment apartment;
  if (!apartment.usable())
  {
    return false;
  }

  Microsoft::WRL::ComPtr<IAccessible> accessible;
  ScopedVariant child;
  const bool is_browser_window = isSupportedBrowserWindow(root_window);
  SmartRegionCandidate direct_candidate;
  MsaaRegionProperties direct_properties;
  if (SUCCEEDED(AccessibleObjectFromPoint(screen_point, &accessible,
                                          child.address())) &&
      accessible != nullptr &&
      makeCandidateFromAccessible(root_window, accessible.Get(), child.value(),
                                  screen_point, 0, direct_candidate,
                                  &direct_properties,
                                  filteredNodeDiagnostic(diagnostic)))
  {
    if (diagnostic != nullptr)
    {
      diagnostic->path = SmartRegionMsaaTraversalPath::DirectPoint;
    }
    if (msaaShouldDeferDirectBrowserContainerCandidate(
            is_browser_window, direct_properties))
    {
      SmartRegionCandidate descendant_candidate;
      if (locateRootScopedMsaaCandidate(root_window, screen_point,
                                        descendant_candidate, diagnostic,
                                        out_browser_semantic_miss))
      {
        out = descendant_candidate;
        return true;
      }
    }
    setMsaaTraversalStopReason(
        diagnostic, SmartRegionMsaaTraversalStopReason::CandidateFound);
    out = direct_candidate;
    return true;
  }
  return locateRootScopedMsaaCandidate(root_window, screen_point, out,
                                       diagnostic, out_browser_semantic_miss);
}

}  // namespace qingying::window_detail
