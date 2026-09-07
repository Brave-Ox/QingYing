#include <cstddef>

#include <Windows.h>
#include <UIAutomation.h>
#include <wrl/client.h>

#include "uia_region_locator.hpp"

namespace qingying::window_detail {
namespace {

constexpr DWORD kUiaConnectionTimeoutMs = 50;
constexpr DWORD kUiaTransactionTimeoutMs = 50;
constexpr std::size_t kMaximumUiaAncestors = 4;

class ScopedComApartment {
 public:
  ScopedComApartment() noexcept
      : m_result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)),
        m_should_uninitialize(SUCCEEDED(m_result))
  {
  }

  ~ScopedComApartment()
  {
    if (m_should_uninitialize) {
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

UiaControlType mapControlType(CONTROLTYPEID control_type) noexcept
{
  switch (control_type) {
    case UIA_ButtonControlTypeId:
      return UiaControlType::Button;
    case UIA_CheckBoxControlTypeId:
      return UiaControlType::CheckBox;
    case UIA_ComboBoxControlTypeId:
      return UiaControlType::ComboBox;
    case UIA_DataItemControlTypeId:
      return UiaControlType::DataItem;
    case UIA_EditControlTypeId:
      return UiaControlType::Edit;
    case UIA_HyperlinkControlTypeId:
      return UiaControlType::Hyperlink;
    case UIA_ListItemControlTypeId:
      return UiaControlType::ListItem;
    case UIA_MenuItemControlTypeId:
      return UiaControlType::MenuItem;
    case UIA_TabItemControlTypeId:
      return UiaControlType::TabItem;
    case UIA_TreeItemControlTypeId:
      return UiaControlType::TreeItem;
    case UIA_DocumentControlTypeId:
      return UiaControlType::Document;
    case UIA_ListControlTypeId:
      return UiaControlType::List;
    case UIA_TreeControlTypeId:
      return UiaControlType::Tree;
    case UIA_DataGridControlTypeId:
      return UiaControlType::DataGrid;
    case UIA_TableControlTypeId:
      return UiaControlType::Table;
    default:
      break;
  }
  return UiaControlType::Unknown;
}

SmartRegionSemantic semanticFor(UiaControlType control_type) noexcept
{
  switch (control_type) {
    case UiaControlType::Button:
    case UiaControlType::CheckBox:
    case UiaControlType::ComboBox:
    case UiaControlType::DataItem:
    case UiaControlType::Edit:
    case UiaControlType::Hyperlink:
    case UiaControlType::ListItem:
    case UiaControlType::MenuItem:
    case UiaControlType::TabItem:
    case UiaControlType::TreeItem:
      return SmartRegionSemantic::ActionableControl;
    case UiaControlType::Document:
    case UiaControlType::List:
    case UiaControlType::Tree:
    case UiaControlType::DataGrid:
    case UiaControlType::Table:
      return SmartRegionSemantic::ContentSurface;
    case UiaControlType::Unknown:
      break;
  }
  return SmartRegionSemantic::Unknown;
}

bool readProperties(IUIAutomationElement* element,
                    UiaRegionProperties& out) noexcept
{
  out = UiaRegionProperties{};
  if (element == nullptr) {
    return false;
  }

  RECT rect{};
  CONTROLTYPEID control_type = 0;
  BOOL is_control = FALSE;
  BOOL is_content = FALSE;
  if (FAILED(element->get_CurrentBoundingRectangle(&rect)) ||
      FAILED(element->get_CurrentControlType(&control_type)) ||
      FAILED(element->get_CurrentIsControlElement(&is_control)) ||
      FAILED(element->get_CurrentIsContentElement(&is_content)) ||
      rect.right <= rect.left || rect.bottom <= rect.top) {
    return false;
  }

  out.rect = {rect.left, rect.top, rect.right, rect.bottom};
  out.control_type = mapControlType(control_type);
  out.is_control = is_control != FALSE;
  out.is_content = is_content != FALSE;
  return true;
}

bool getWindowRect(HWND window, WindowRect& out) noexcept
{
  RECT rect{};
  if (window == nullptr || !GetWindowRect(window, &rect) ||
      rect.right <= rect.left || rect.bottom <= rect.top) {
    return false;
  }
  out = {rect.left, rect.top, rect.right, rect.bottom};
  return true;
}

}  // namespace

bool makeUiaCandidate(HWND root_window, POINT screen_point,
                      const UiaRegionProperties& properties,
                      SmartRegionCandidate& out) noexcept
{
  out = SmartRegionCandidate{};
  const SmartRegionSemantic semantic = semanticFor(properties.control_type);
  const bool supports_semantic = semantic != SmartRegionSemantic::Unknown;
  const bool contains_point = screen_point.x >= properties.rect.left &&
                              screen_point.x < properties.rect.right &&
                              screen_point.y >= properties.rect.top &&
                              screen_point.y < properties.rect.bottom;
  if (root_window == nullptr || !supports_semantic || !contains_point ||
      (!properties.is_control && !properties.is_content)) {
    return false;
  }

  SmartRegionCandidate candidate{
      reinterpret_cast<std::uintptr_t>(root_window),
      reinterpret_cast<std::uintptr_t>(root_window), properties.rect,
      SmartRegionKind::KnownContent};
  candidate.source = SmartRegionDiagnosticSource::Uia;
  candidate.semantic = semantic;
  if (!candidate.valid()) {
    return false;
  }
  out = candidate;
  return true;
}

std::size_t collectUiaCandidates(
    HWND root_window, POINT screen_point,
    const UiaRegionProperties* properties, std::size_t property_count,
    SmartRegionCandidate* out_candidates, std::size_t capacity) noexcept
{
  if (root_window == nullptr || properties == nullptr ||
      out_candidates == nullptr || capacity == 0) {
    return 0;
  }

  std::size_t candidate_count = 0;
  for (std::size_t index = 0;
       index < property_count && candidate_count < capacity; ++index) {
    SmartRegionCandidate candidate;
    if (makeUiaCandidate(root_window, screen_point, properties[index],
                         candidate)) {
      out_candidates[candidate_count++] = candidate;
    }
  }
  return candidate_count;
}

bool locateUiaCandidates(HWND root_window, POINT screen_point,
                         SmartRegionCandidate* out_candidates,
                         std::size_t capacity,
                         std::size_t& out_count) noexcept
{
  out_count = 0;
  WindowRect root_rect;
  if (root_window == nullptr || !IsWindow(root_window) ||
      !IsWindowVisible(root_window) || !getWindowRect(root_window, root_rect) ||
      out_candidates == nullptr || capacity == 0) {
    return false;
  }

  ScopedComApartment com_apartment;
  if (!com_apartment.usable()) {
    return false;
  }

  Microsoft::WRL::ComPtr<IUIAutomation2> automation;
  if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr,
                               CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(&automation))) ||
      automation == nullptr ||
      FAILED(automation->put_ConnectionTimeout(kUiaConnectionTimeoutMs)) ||
      FAILED(automation->put_TransactionTimeout(kUiaTransactionTimeoutMs))) {
    return false;
  }

  Microsoft::WRL::ComPtr<IUIAutomationElement> element;
  if (FAILED(automation->ElementFromPoint(screen_point, &element)) ||
      element == nullptr) {
    return false;
  }

  Microsoft::WRL::ComPtr<IUIAutomationTreeWalker> walker;
  static_cast<void>(automation->get_ControlViewWalker(&walker));

  UiaRegionProperties properties[kMaximumUiaAncestors];
  std::size_t property_count = 0;
  for (std::size_t depth = 0; depth < kMaximumUiaAncestors; ++depth) {
    UiaRegionProperties properties_at_depth;
    if (readProperties(element.Get(), properties_at_depth)) {
      properties[property_count++] = properties_at_depth;
    }

    if (walker == nullptr) {
      break;
    }
    Microsoft::WRL::ComPtr<IUIAutomationElement> parent;
    if (FAILED(walker->GetParentElement(element.Get(), &parent)) ||
        parent == nullptr) {
      break;
    }
    element = parent;
  }

  out_count = collectUiaCandidates(root_window, screen_point, properties,
                                   property_count, out_candidates, capacity);
  return out_count != 0;
}

}  // namespace qingying::window_detail
