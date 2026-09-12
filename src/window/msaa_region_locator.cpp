#include "msaa_region_locator.hpp"

#include <limits>

#include <wrl/client.h>

namespace qingying::window_detail {
namespace {

constexpr std::uint8_t MaximumMsaaHitTestDepth = 6;
constexpr LONG MaximumRootContentInsetPx = 1;

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

bool contains(const WindowRect& rect, POINT point) noexcept
{
  return !rect.empty() && point.x >= rect.left && point.x < rect.right &&
         point.y >= rect.top && point.y < rect.bottom;
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
    case ROLE_SYSTEM_GROUPING:
    case ROLE_SYSTEM_GRAPHIC:
      return SmartRegionSemantic::ContentSurface;
    default:
      break;
  }
  return SmartRegionSemantic::Unknown;
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

bool makeCandidateFromAccessible(HWND root_window,
                                 IAccessible* accessible,
                                 const VARIANT& child,
                                 POINT screen_point,
                                 std::uint8_t accessibility_depth,
                                 SmartRegionCandidate& out) noexcept
{
  if (accessible == nullptr)
  {
    return false;
  }

  ScopedVariant role;
  ScopedVariant state;
  if (FAILED(accessible->get_accRole(child, role.address())) ||
      role.value().vt != VT_I4)
  {
    return false;
  }
  static_cast<void>(accessible->get_accState(child, state.address()));

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

  HWND target_window = nullptr;
  if (FAILED(WindowFromAccessibleObject(accessible, &target_window)))
  {
    target_window = root_window;
  }
  const LONG state_value = state.value().vt == VT_I4 ? state.value().lVal : 0;
  const MsaaRegionProperties properties{
      {left, top, left + width, top + height}, role.value().lVal, state_value};
  return makeMsaaCandidate(root_window, target_window, screen_point,
                           properties, out, accessibility_depth);
}

bool rememberCandidateFromAccessible(HWND root_window,
                                    IAccessible* accessible,
                                    const VARIANT& child,
                                    POINT screen_point,
                                    std::uint8_t accessibility_depth,
                                    SmartRegionCandidate& best_candidate,
                                    bool& has_candidate) noexcept
{
  SmartRegionCandidate candidate;
  if (!makeCandidateFromAccessible(root_window, accessible, child,
                                   screen_point, accessibility_depth,
                                   candidate))
  {
    return false;
  }
  best_candidate = candidate;
  has_candidate = true;
  return true;
}

bool locateRootScopedMsaaCandidate(HWND root_window, POINT screen_point,
                                   SmartRegionCandidate& out) noexcept
{
  Microsoft::WRL::ComPtr<IAccessible> root_accessible;
  if (FAILED(AccessibleObjectFromWindow(
          root_window, static_cast<DWORD>(OBJID_CLIENT), IID_IAccessible,
          reinterpret_cast<void**>(root_accessible.GetAddressOf()))) ||
      root_accessible == nullptr)
  {
    return false;
  }

  Microsoft::WRL::ComPtr<IAccessible> current_accessible = root_accessible;
  SmartRegionCandidate best_candidate;
  bool has_candidate = false;
  std::uint8_t accessibility_depth = 0;
  while (accessibility_depth < MaximumMsaaHitTestDepth)
  {
    ScopedVariant hit;
    if (FAILED(current_accessible->accHitTest(screen_point.x, screen_point.y,
                                               hit.address())))
    {
      break;
    }
    const std::uint8_t child_depth =
        static_cast<std::uint8_t>(accessibility_depth + 1);
    if (hit.value().vt == VT_I4)
    {
      static_cast<void>(rememberCandidateFromAccessible(
          root_window, current_accessible.Get(), hit.value(), screen_point,
          child_depth, best_candidate, has_candidate));

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
    static_cast<void>(rememberCandidateFromAccessible(
        root_window, child_accessible.Get(), self.value(), screen_point,
        child_depth, best_candidate, has_candidate));
    if (child_accessible.Get() == current_accessible.Get())
    {
      break;
    }
    current_accessible = child_accessible;
    accessibility_depth = child_depth;
  }

  if (has_candidate)
  {
    out = best_candidate;
    return true;
  }
  ScopedVariant self;
  self.setLong(CHILDID_SELF);
  return makeCandidateFromAccessible(root_window, current_accessible.Get(),
                                     self.value(), screen_point,
                                     accessibility_depth, out);
}

}  // namespace

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
                       std::uint8_t accessibility_depth) noexcept
{
  out = SmartRegionCandidate{};
  const SmartRegionSemantic semantic =
      semanticForRole(properties.role, properties.state);
  if (root_window == nullptr || semantic == SmartRegionSemantic::Unknown ||
      !targetBelongsToRoot(root_window, target_window) ||
      (semantic == SmartRegionSemantic::ContentSurface &&
       isWindowSizedContentSurface(root_window, properties.rect)) ||
      (properties.state & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN)) !=
          0 ||
      !contains(properties.rect, screen_point))
  {
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
                         SmartRegionCandidate& out) noexcept
{
  out = SmartRegionCandidate{};
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
  if (SUCCEEDED(AccessibleObjectFromPoint(screen_point, &accessible,
                                          child.address())) &&
      accessible != nullptr &&
      makeCandidateFromAccessible(root_window, accessible.Get(), child.value(),
                                  screen_point, 0, out))
  {
    return true;
  }
  return locateRootScopedMsaaCandidate(root_window, screen_point, out);
}

}  // namespace qingying::window_detail
