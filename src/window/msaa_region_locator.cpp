#include "msaa_region_locator.hpp"

#include <limits>

#include <wrl/client.h>

namespace qingying::window_detail {
namespace {

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

}  // namespace

bool makeMsaaCandidate(HWND root_window, HWND target_window,
                       POINT screen_point,
                       const MsaaRegionProperties& properties,
                       SmartRegionCandidate& out) noexcept
{
  out = SmartRegionCandidate{};
  const SmartRegionSemantic semantic =
      semanticForRole(properties.role, properties.state);
  if (root_window == nullptr || semantic == SmartRegionSemantic::Unknown ||
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
  if (FAILED(AccessibleObjectFromPoint(screen_point, &accessible,
                                       child.address())) ||
      accessible == nullptr)
  {
    return false;
  }

  ScopedVariant role;
  ScopedVariant state;
  if (FAILED(accessible->get_accRole(child.value(), role.address())) ||
      role.value().vt != VT_I4)
  {
    return false;
  }
  static_cast<void>(accessible->get_accState(child.value(), state.address()));

  LONG left = 0;
  LONG top = 0;
  LONG width = 0;
  LONG height = 0;
  constexpr LONG MaximumCoordinate = (std::numeric_limits<LONG>::max)();
  if (FAILED(accessible->accLocation(&left, &top, &width, &height,
                                     child.value())) ||
      width <= 0 || height <= 0 || left > MaximumCoordinate - width ||
      top > MaximumCoordinate - height)
  {
    return false;
  }

  HWND target_window = nullptr;
  if (FAILED(WindowFromAccessibleObject(accessible.Get(), &target_window)))
  {
    target_window = root_window;
  }
  const LONG state_value = state.value().vt == VT_I4 ? state.value().lVal : 0;
  const MsaaRegionProperties properties{
      {left, top, left + width, top + height}, role.value().lVal, state_value};
  return makeMsaaCandidate(root_window, target_window, screen_point,
                           properties, out);
}

}  // namespace qingying::window_detail
