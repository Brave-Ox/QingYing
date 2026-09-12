#include <cstddef>

#include <array>

#include <Windows.h>
#include <UIAutomation.h>
#include <wrl/client.h>

#include "uia_region_locator.hpp"

namespace qingying::window_detail {
namespace {

constexpr DWORD kUiaConnectionTimeoutMs = 50;
constexpr DWORD kUiaTransactionTimeoutMs = 50;
constexpr std::uint64_t kUiaQueryBudgetMs = 80;
constexpr std::size_t kMaximumUiaPathDepth = 12;
constexpr std::size_t kMaximumUiaChildrenPerLevel = 64;
constexpr int kMinimumGenericContainerWidth = 32;
constexpr int kMinimumGenericContainerHeight = 24;
constexpr int kMinimumCompactControlWidth = 12;
constexpr int kMinimumCompactControlHeight = 12;
constexpr std::int64_t kMaximumRootScopedCandidatePercent = 35;

class ScopedBstr
{
 public:
  ScopedBstr() noexcept = default;

  ~ScopedBstr()
  {
    SysFreeString(m_value);
  }

  ScopedBstr(const ScopedBstr&) = delete;
  ScopedBstr& operator=(const ScopedBstr&) = delete;

  BSTR* address() noexcept
  {
    return &m_value;
  }

  bool empty() const noexcept
  {
    return m_value == nullptr || SysStringLen(m_value) == 0;
  }

 private:
  BSTR m_value{nullptr};
};

class ScopedComApartment {
 public:
  explicit ScopedComApartment(DWORD apartment_type) noexcept
      : m_result(CoInitializeEx(nullptr, apartment_type)),
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
    case UIA_RadioButtonControlTypeId:
      return UiaControlType::RadioButton;
    case UIA_ComboBoxControlTypeId:
      return UiaControlType::ComboBox;
    case UIA_DataItemControlTypeId:
      return UiaControlType::DataItem;
    case UIA_EditControlTypeId:
      return UiaControlType::Edit;
    case UIA_HyperlinkControlTypeId:
      return UiaControlType::Hyperlink;
    case UIA_ImageControlTypeId:
      return UiaControlType::Image;
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
    case UIA_PaneControlTypeId:
      return UiaControlType::Pane;
    case UIA_GroupControlTypeId:
      return UiaControlType::Group;
    case UIA_CustomControlTypeId:
      return UiaControlType::Custom;
    default:
      break;
  }
  return UiaControlType::Unknown;
}

SmartRegionSemantic semanticFor(
    const UiaRegionProperties& properties) noexcept
{
  if (!properties.is_enabled)
  {
    return SmartRegionSemantic::Unknown;
  }
  switch (properties.control_type) {
    case UiaControlType::Button:
    case UiaControlType::CheckBox:
    case UiaControlType::RadioButton:
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
    case UiaControlType::Image:
    case UiaControlType::List:
    case UiaControlType::Tree:
    case UiaControlType::DataGrid:
    case UiaControlType::Table:
      return SmartRegionSemantic::ContentSurface;
    case UiaControlType::Pane:
    case UiaControlType::Group:
    case UiaControlType::Custom:
      if (!properties.is_enabled)
      {
        return SmartRegionSemantic::Unknown;
      }
      if (properties.is_control && properties.is_keyboard_focusable)
      {
        return SmartRegionSemantic::ActionableControl;
      }
      if (properties.is_control && !properties.is_content &&
          properties.rect.width() >= kMinimumCompactControlWidth &&
          properties.rect.height() >= kMinimumCompactControlHeight)
      {
        return SmartRegionSemantic::ActionableControl;
      }
      if (!properties.has_name)
      {
        return SmartRegionSemantic::Unknown;
      }
      if (properties.is_content &&
          properties.rect.width() >= kMinimumGenericContainerWidth &&
          properties.rect.height() >= kMinimumGenericContainerHeight)
      {
        return SmartRegionSemantic::ContentSurface;
      }
      return SmartRegionSemantic::Unknown;
    case UiaControlType::Unknown:
      break;
  }
  return SmartRegionSemantic::Unknown;
}

SmartRegionUiaQuality qualityFor(
    const UiaRegionProperties& properties,
    SmartRegionSemantic semantic) noexcept
{
  if (!properties.is_enabled)
  {
    return SmartRegionUiaQuality::Disabled;
  }
  if (semantic == SmartRegionSemantic::ActionableControl)
  {
    return properties.has_name ? SmartRegionUiaQuality::NamedActionable
                                : SmartRegionUiaQuality::UnnamedActionable;
  }
  if (semantic == SmartRegionSemantic::ContentSurface)
  {
    return SmartRegionUiaQuality::ContentSurface;
  }
  if (properties.control_type == UiaControlType::Pane ||
      properties.control_type == UiaControlType::Group ||
      properties.control_type == UiaControlType::Custom)
  {
    return SmartRegionUiaQuality::GenericContainer;
  }
  return SmartRegionUiaQuality::None;
}

void appendCachedPattern(IUIAutomationElement* element, PATTERNID pattern_id,
                         UiaPatternFlag flag, std::uint8_t& out_flags) noexcept
{
  if (element == nullptr)
  {
    return;
  }
  Microsoft::WRL::ComPtr<IUnknown> pattern;
  if (SUCCEEDED(element->GetCachedPattern(pattern_id, &pattern)) &&
      pattern != nullptr)
  {
    out_flags |= static_cast<std::uint8_t>(flag);
  }
}

bool readCachedProperties(IUIAutomationElement* element,
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
  BOOL is_enabled = TRUE;
  BOOL is_keyboard_focusable = FALSE;
  UIA_HWND native_window = nullptr;
  ScopedBstr name;
  if (FAILED(element->get_CachedBoundingRectangle(&rect)) ||
      FAILED(element->get_CachedControlType(&control_type)) ||
      FAILED(element->get_CachedIsControlElement(&is_control)) ||
      FAILED(element->get_CachedIsContentElement(&is_content)) ||
      rect.right <= rect.left || rect.bottom <= rect.top) {
    return false;
  }

  out.rect = {rect.left, rect.top, rect.right, rect.bottom};
  out.control_type = mapControlType(control_type);
  out.control_type_id = static_cast<std::uint32_t>(control_type);
  out.is_control = is_control != FALSE;
  out.is_content = is_content != FALSE;
  if (SUCCEEDED(element->get_CachedIsEnabled(&is_enabled)))
  {
    out.is_enabled = is_enabled != FALSE;
  }
  if (SUCCEEDED(
          element->get_CachedIsKeyboardFocusable(&is_keyboard_focusable)))
  {
    out.is_keyboard_focusable = is_keyboard_focusable != FALSE;
  }
  if (SUCCEEDED(element->get_CachedName(name.address())))
  {
    out.has_name = !name.empty();
  }
  if (SUCCEEDED(element->get_CachedNativeWindowHandle(&native_window)))
  {
    out.native_window = reinterpret_cast<HWND>(native_window);
  }
  appendCachedPattern(element, UIA_InvokePatternId, UiaPatternFlag::Invoke,
                      out.supported_pattern_flags);
  appendCachedPattern(element, UIA_TogglePatternId, UiaPatternFlag::Toggle,
                      out.supported_pattern_flags);
  appendCachedPattern(element, UIA_SelectionItemPatternId,
                      UiaPatternFlag::SelectionItem,
                      out.supported_pattern_flags);
  appendCachedPattern(element, UIA_ExpandCollapsePatternId,
                      UiaPatternFlag::ExpandCollapse,
                      out.supported_pattern_flags);
  appendCachedPattern(element, UIA_ValuePatternId, UiaPatternFlag::Value,
                      out.supported_pattern_flags);
  appendCachedPattern(element, UIA_RangeValuePatternId,
                      UiaPatternFlag::RangeValue,
                      out.supported_pattern_flags);
  appendCachedPattern(element, UIA_ScrollItemPatternId,
                      UiaPatternFlag::ScrollItem,
                      out.supported_pattern_flags);
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

bool configureCacheRequest(
    IUIAutomationCacheRequest* cache_request) noexcept
{
  return cache_request != nullptr &&
         SUCCEEDED(cache_request->put_TreeScope(TreeScope_Element)) &&
         SUCCEEDED(cache_request->put_AutomationElementMode(
             AutomationElementMode_Full)) &&
         SUCCEEDED(cache_request->AddProperty(
             UIA_BoundingRectanglePropertyId)) &&
         SUCCEEDED(cache_request->AddProperty(UIA_ControlTypePropertyId)) &&
         SUCCEEDED(cache_request->AddProperty(
             UIA_IsControlElementPropertyId)) &&
         SUCCEEDED(cache_request->AddProperty(
             UIA_IsContentElementPropertyId)) &&
         SUCCEEDED(cache_request->AddProperty(UIA_IsEnabledPropertyId)) &&
         SUCCEEDED(cache_request->AddProperty(
             UIA_IsKeyboardFocusablePropertyId)) &&
         SUCCEEDED(cache_request->AddProperty(UIA_NamePropertyId)) &&
         SUCCEEDED(cache_request->AddProperty(
             UIA_NativeWindowHandlePropertyId)) &&
         SUCCEEDED(cache_request->AddPattern(UIA_InvokePatternId)) &&
         SUCCEEDED(cache_request->AddPattern(UIA_TogglePatternId)) &&
         SUCCEEDED(cache_request->AddPattern(UIA_SelectionItemPatternId)) &&
         SUCCEEDED(cache_request->AddPattern(UIA_ExpandCollapsePatternId)) &&
         SUCCEEDED(cache_request->AddPattern(UIA_ValuePatternId)) &&
         SUCCEEDED(cache_request->AddPattern(UIA_RangeValuePatternId)) &&
         SUCCEEDED(cache_request->AddPattern(UIA_ScrollItemPatternId));
}

}  // namespace

struct UiaRegionLocatorSession::Impl
{
  bool initialize() noexcept
  {
    if (automation != nullptr && cache_request != nullptr && walker != nullptr &&
        true_condition != nullptr)
    {
      return true;
    }

    automation.Reset();
    cache_request.Reset();
    walker.Reset();
    true_condition.Reset();
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&automation))) ||
        automation == nullptr ||
        FAILED(automation->put_ConnectionTimeout(kUiaConnectionTimeoutMs)) ||
        FAILED(automation->put_TransactionTimeout(kUiaTransactionTimeoutMs)) ||
        FAILED(automation->CreateCacheRequest(&cache_request)) ||
        !configureCacheRequest(cache_request.Get()) ||
        FAILED(automation->get_ControlViewWalker(&walker)) || walker == nullptr)
    {
      automation.Reset();
      cache_request.Reset();
      walker.Reset();
      return false;
    }
    if (FAILED(automation->CreateTrueCondition(&true_condition)) ||
        true_condition == nullptr)
    {
      automation.Reset();
      cache_request.Reset();
      walker.Reset();
      return false;
    }
    return true;
  }

  bool collectDesktopPath(
      HWND root_window, POINT screen_point, std::uint64_t begin_ms,
      UiaRegionProperties* out_path, std::size_t capacity,
      std::size_t& out_count) noexcept
  {
    out_count = 0;
    Microsoft::WRL::ComPtr<IUIAutomationElement> element;
    if (FAILED(automation->ElementFromPointBuildCache(
            screen_point, cache_request.Get(), &element)) ||
        element == nullptr)
    {
      return false;
    }

    while (out_count < capacity)
    {
      UiaRegionProperties properties;
      if (readCachedProperties(element.Get(), properties))
      {
        out_path[out_count++] = properties;
        if (properties.native_window == root_window)
        {
          break;
        }
      }
      if (GetTickCount64() - begin_ms >= kUiaQueryBudgetMs)
      {
        break;
      }

      Microsoft::WRL::ComPtr<IUIAutomationElement> parent;
      if (FAILED(walker->GetParentElementBuildCache(
              element.Get(), cache_request.Get(), &parent)) ||
          parent == nullptr)
      {
        break;
      }
      element = parent;
    }
    return out_count != 0;
  }

  bool collectRootScopedPath(
      HWND root_window, POINT screen_point, std::uint64_t begin_ms,
      UiaRegionProperties* out_path, std::size_t capacity,
      std::size_t& out_count) noexcept
  {
    out_count = 0;
    Microsoft::WRL::ComPtr<IUIAutomationElement> element;
    if (FAILED(automation->ElementFromHandleBuildCache(
            root_window, cache_request.Get(), &element)) ||
        element == nullptr)
    {
      return false;
    }

    while (out_count < capacity)
    {
      UiaRegionProperties properties;
      if (readCachedProperties(element.Get(), properties))
      {
        out_path[out_count++] = properties;
      }
      if (GetTickCount64() - begin_ms >= kUiaQueryBudgetMs)
      {
        break;
      }

      Microsoft::WRL::ComPtr<IUIAutomationElementArray> children;
      if (FAILED(element->FindAllBuildCache(
              TreeScope_Children, true_condition.Get(), cache_request.Get(),
              &children)) ||
          children == nullptr)
      {
        break;
      }
      int child_count = 0;
      if (FAILED(children->get_Length(&child_count)) || child_count <= 0)
      {
        break;
      }

      std::array<UiaRegionProperties, kMaximumUiaChildrenPerLevel>
          child_properties{};
      std::array<int, kMaximumUiaChildrenPerLevel> child_indices{};
      std::size_t readable_child_count = 0;
      const int limited_child_count =
          (std::min)(child_count,
                     static_cast<int>(kMaximumUiaChildrenPerLevel));
      for (int index = 0; index < limited_child_count; ++index)
      {
        Microsoft::WRL::ComPtr<IUIAutomationElement> child;
        UiaRegionProperties child_at_index;
        if (SUCCEEDED(children->GetElement(index, &child)) &&
            child != nullptr &&
            readCachedProperties(child.Get(), child_at_index))
        {
          child_properties.at(readable_child_count) = child_at_index;
          child_indices.at(readable_child_count) = index;
          ++readable_child_count;
        }
      }

      std::size_t selected_index = 0;
      if (!selectSmallestUiaChildAtPoint(
              child_properties.data(), readable_child_count, screen_point,
              selected_index))
      {
        break;
      }
      Microsoft::WRL::ComPtr<IUIAutomationElement> selected_child;
      if (FAILED(children->GetElement(child_indices.at(selected_index),
                                      &selected_child)) ||
          selected_child == nullptr)
      {
        break;
      }
      element = selected_child;
    }
    return out_count != 0;
  }

  Microsoft::WRL::ComPtr<IUIAutomation2> automation;
  Microsoft::WRL::ComPtr<IUIAutomationCacheRequest> cache_request;
  Microsoft::WRL::ComPtr<IUIAutomationTreeWalker> walker;
  Microsoft::WRL::ComPtr<IUIAutomationCondition> true_condition;
};

UiaRegionLocatorSession::UiaRegionLocatorSession()
    : m_impl(std::make_unique<Impl>())
{
}

UiaRegionLocatorSession::~UiaRegionLocatorSession() = default;

bool selectSmallestUiaChildAtPoint(
    const UiaRegionProperties* children, std::size_t child_count,
    POINT screen_point, std::size_t& out_index) noexcept
{
  if (children == nullptr || child_count == 0)
  {
    return false;
  }

  std::int64_t smallest_area = 0;
  bool found = false;
  for (std::size_t index = 0; index < child_count; ++index)
  {
    const WindowRect& rect = children[index].rect;
    const bool contains_point =
        !rect.empty() && screen_point.x >= rect.left &&
        screen_point.x < rect.right && screen_point.y >= rect.top &&
        screen_point.y < rect.bottom;
    if (!contains_point)
    {
      continue;
    }
    const std::int64_t area =
        static_cast<std::int64_t>(rect.width()) * rect.height();
    if (!found || area < smallest_area)
    {
      found = true;
      smallest_area = area;
      out_index = index;
    }
  }
  return found;
}

bool uiaPathBelongsToRoot(const UiaRegionProperties* path,
                          std::size_t path_count,
                          HWND root_window) noexcept
{
  if (path == nullptr || path_count == 0 || root_window == nullptr)
  {
    return false;
  }
  for (std::size_t index = 0; index < path_count; ++index)
  {
    if (path[index].native_window == root_window)
    {
      return true;
    }
  }
  return false;
}

bool isUsefulRootScopedUiaCandidate(
    const SmartRegionCandidate& candidate,
    const WindowRect& owner_rect) noexcept
{
  if (!candidate.valid() || owner_rect.empty() ||
      candidate.source != SmartRegionDiagnosticSource::Uia ||
      candidate.semantic == SmartRegionSemantic::Unknown ||
      candidate.semantic == SmartRegionSemantic::Fallback)
  {
    return false;
  }

  const std::int64_t owner_area =
      static_cast<std::int64_t>(owner_rect.width()) * owner_rect.height();
  const std::int64_t candidate_area =
      static_cast<std::int64_t>(candidate.rect.width()) *
      candidate.rect.height();
  return owner_area > 0 && candidate_area > 0 &&
         candidate_area * 100 <
             owner_area * kMaximumRootScopedCandidatePercent;
}

bool UiaRegionLocatorSession::locate(
    HWND root_window, POINT screen_point,
    SmartRegionCandidate* out_candidates, std::size_t capacity,
    std::size_t& out_count) noexcept
{
  out_count = 0;
  const std::uint64_t begin_ms = GetTickCount64();
  WindowRect root_rect;
  if (root_window == nullptr || !IsWindow(root_window) ||
      !IsWindowVisible(root_window) || !getWindowRect(root_window, root_rect) ||
      out_candidates == nullptr || capacity == 0 || !m_impl->initialize())
  {
    return false;
  }

  UiaRegionProperties properties[kMaximumUiaPathDepth];
  std::size_t property_count = 0;
  bool used_root_scoped_fallback = false;
  static_cast<void>(m_impl->collectDesktopPath(
      root_window, screen_point, begin_ms, properties,
      kMaximumUiaPathDepth, property_count));
  if (!uiaPathBelongsToRoot(properties, property_count, root_window))
  {
    property_count = 0;
    used_root_scoped_fallback = true;
    static_cast<void>(m_impl->collectRootScopedPath(
        root_window, screen_point, begin_ms, properties,
        kMaximumUiaPathDepth, property_count));
  }

  out_count = collectUiaCandidates(root_window, screen_point, properties,
                                   property_count, out_candidates, capacity);
  if (used_root_scoped_fallback)
  {
    std::size_t retained_count = 0;
    for (std::size_t index = 0; index < out_count; ++index)
    {
      if (isUsefulRootScopedUiaCandidate(out_candidates[index], root_rect))
      {
        out_candidates[retained_count++] = out_candidates[index];
      }
    }
    out_count = retained_count;
  }
  return out_count != 0;
}

bool makeUiaCandidate(HWND root_window, POINT screen_point,
                      const UiaRegionProperties& properties,
                      SmartRegionCandidate& out) noexcept
{
  out = SmartRegionCandidate{};
  const SmartRegionSemantic semantic = semanticFor(properties);
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
  candidate.uia_metadata.available = true;
  candidate.uia_metadata.is_control_element = properties.is_control;
  candidate.uia_metadata.is_content_element = properties.is_content;
  candidate.uia_metadata.is_enabled = properties.is_enabled;
  candidate.uia_metadata.is_keyboard_focusable =
      properties.is_keyboard_focusable;
  candidate.uia_metadata.has_name = properties.has_name;
  candidate.uia_metadata.control_type_id = properties.control_type_id;
  candidate.uia_metadata.pattern_flags =
      properties.supported_pattern_flags;
  candidate.uia_metadata.quality = qualityFor(properties, semantic);
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
  ScopedComApartment com_apartment(COINIT_APARTMENTTHREADED);
  if (!com_apartment.usable())
  {
    out_count = 0;
    return false;
  }
  UiaRegionLocatorSession session;
  return session.locate(root_window, screen_point, out_candidates, capacity,
                        out_count);
}

}  // namespace qingying::window_detail
