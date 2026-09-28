#include <algorithm>
#include <array>
#include <cwchar>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <Windows.h>

#include "qingying/app/settings_window.hpp"
#include "qingying/app/settings_window_model.hpp"
#include "resource.h"

namespace qingying {
namespace {

constexpr wchar_t SettingsWindowClassName[] = L"QingYing.SettingsWindow";
constexpr int BaseDpi = 96;
constexpr int DefaultWidth = 760;
constexpr int DefaultHeight = 560;
constexpr int MinimumWidth = 720;
constexpr int MinimumHeight = 520;
constexpr int NavigationWidth = 208;
constexpr int FooterHeight = 76;
constexpr int ContentPadding = 36;
constexpr int GeneralCardBottom = 310;
constexpr int GeneralCardContentInset = 24;
constexpr int GeneralRowHeight = 54;
constexpr int GeneralFirstRowTop = 152;
constexpr int GeneralSecondRowTop = 234;
constexpr int GeneralRowDividerY = 220;
constexpr int GeneralCheckboxSize = 20;
constexpr int GeneralCheckboxRadius = 4;
constexpr int GeneralCheckboxTextGap = 14;
constexpr int GeneralCheckmarkStroke = 2;
constexpr int GeneralCheckmarkPointCount = 3;
constexpr int HotkeyRowTop = 142;
constexpr int HotkeyRowHeight = 72;
constexpr int HotkeyRowDividerOffset = 62;
constexpr int LongShotFirstRowTop = 146;
constexpr int LongShotSecondRowTop = 202;
constexpr int LongShotRowDividerY = 196;
constexpr int LongShotTextBlockHeight = 46;
constexpr int LongShotControlHeight = 30;
constexpr int LongShotControlTopOffset =
    (LongShotTextBlockHeight - LongShotControlHeight) / 2;
constexpr int StepperValueWidth = 76;
constexpr int StepperButtonSize = 30;
constexpr int StepperGap = 6;
constexpr int HotkeyInputRadius = 6;
constexpr int StepperButtonRadius = 6;
constexpr COLORREF PrimaryColor = RGB(59, 130, 246);
constexpr COLORREF PrimaryHoverColor = RGB(37, 99, 235);
constexpr COLORREF NavigationBackgroundColor = RGB(247, 248, 250);
constexpr COLORREF ContentBackgroundColor = RGB(255, 255, 255);
constexpr COLORREF PageBackgroundColor = RGB(251, 252, 254);
constexpr COLORREF SelectedNavigationColor = RGB(234, 242, 255);
constexpr COLORREF BorderColor = RGB(229, 231, 235);
constexpr COLORREF ContentCardBorderColor = RGB(229, 233, 239);
constexpr COLORREF ControlBorderColor = RGB(221, 227, 234);
constexpr COLORREF ControlHoverColor = RGB(243, 247, 255);
constexpr COLORREF PrimaryTextColor = RGB(31, 35, 41);
constexpr COLORREF SecondaryTextColor = RGB(100, 106, 115);
constexpr COLORREF HelperTextColor = RGB(122, 130, 141);
constexpr COLORREF SettingsRowDividerColor = RGB(236, 239, 244);
constexpr COLORREF FocusBorderColor = RGB(191, 219, 254);
constexpr COLORREF ErrorTextColor = RGB(180, 57, 45);
constexpr COLORREF StatusTextColor = RGB(67, 82, 68);

enum ControlId
{
  NavigationGeneral = 1001,
  NavigationHotkeys,
  NavigationLongShot,
  AutostartCheck,
  AgentCheck,
  CaptureHotkeyButton,
  CopyShortcutButton,
  LongShotShortcutButton,
  MaxFramesEdit,
  MaxFramesDecrease,
  MaxFramesIncrease,
  MaxHeightEdit,
  MaxHeightDecrease,
  MaxHeightIncrease,
  RestoreDefaultsButton,
  CancelButton,
  ApplyButton,
  BannerLabel,
  GeneralTitle,
  GeneralAbout,
  HotkeysTitle,
  HotkeysDescription,
  LongShotTitle,
  LongShotDescription,
  FieldErrorLabel,
  LongShotWarningLabel,
};

int scale(int logical_pixels, UINT dpi) noexcept
{
  return MulDiv(logical_pixels, static_cast<int>(dpi), BaseDpi);
}

class FontHandle final
{
 public:
  FontHandle() = default;

  ~FontHandle()
  {
    reset();
  }

  FontHandle(const FontHandle&) = delete;
  FontHandle& operator=(const FontHandle&) = delete;

  void create(int point_size, int weight, UINT dpi)
  {
    reset();
    const int height = -MulDiv(point_size, static_cast<int>(dpi), 72);
    m_font = CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  }

  HFONT get() const noexcept
  {
    return m_font;
  }

 private:
  void reset() noexcept
  {
    if (m_font != nullptr)
    {
      DeleteObject(m_font);
      m_font = nullptr;
    }
  }

  HFONT m_font{nullptr};
};

class BrushHandle final
{
 public:
  explicit BrushHandle(COLORREF color)
      : m_brush(CreateSolidBrush(color))
  {
  }

  ~BrushHandle()
  {
    if (m_brush != nullptr)
    {
      DeleteObject(m_brush);
    }
  }

  HBRUSH get() const noexcept
  {
    return m_brush;
  }

 private:
  HBRUSH m_brush{nullptr};
};

class PenHandle final
{
 public:
  explicit PenHandle(COLORREF color, int width = 1)
      : m_pen(CreatePen(PS_SOLID, width, color))
  {
  }

  ~PenHandle()
  {
    if (m_pen != nullptr)
    {
      DeleteObject(m_pen);
    }
  }

  HPEN get() const noexcept
  {
    return m_pen;
  }

 private:
  HPEN m_pen{nullptr};
};

void drawRoundedRectangle(HDC dc, const RECT& rect, int radius,
                          COLORREF fill_color, COLORREF border_color)
{
  BrushHandle brush(fill_color);
  PenHandle pen(border_color);
  const HGDIOBJ previous_brush = SelectObject(dc, brush.get());
  const HGDIOBJ previous_pen = SelectObject(dc, pen.get());
  RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
  SelectObject(dc, previous_pen);
  SelectObject(dc, previous_brush);
}

void drawText(HDC dc, const RECT& rect, const std::wstring& text, HFONT font,
              COLORREF color, UINT format)
{
  const HGDIOBJ previous_font = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, color);
  RECT text_rect = rect;
  DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &text_rect,
            format | DT_SINGLELINE | DT_VCENTER);
  SelectObject(dc, previous_font);
}

std::wstring controlText(HWND control)
{
  const int length = GetWindowTextLengthW(control);
  std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
  if (length > 0)
  {
    GetWindowTextW(control, text.data(), length + 1);
  }
  text.resize(static_cast<std::size_t>(length));
  return text;
}

std::wstring shortcutText(const ShortcutBinding& binding)
{
  if (binding.empty())
  {
    return L"未设置";
  }
  std::wstring text;
  const auto append = [&text](const wchar_t* value)
  {
    if (!text.empty())
    {
      text += L" + ";
    }
    text += value;
  };
  if ((binding.m_modifiers & MOD_CONTROL) != 0)
  {
    append(L"Ctrl");
  }
  if ((binding.m_modifiers & MOD_SHIFT) != 0)
  {
    append(L"Shift");
  }
  if ((binding.m_modifiers & MOD_ALT) != 0)
  {
    append(L"Alt");
  }
  if ((binding.m_modifiers & MOD_WIN) != 0)
  {
    append(L"Win");
  }
  if (!text.empty())
  {
    text += L" + ";
  }
  if (binding.m_virtual_key >= '0' && binding.m_virtual_key <= 'Z')
  {
    text += static_cast<wchar_t>(binding.m_virtual_key);
  }
  else if (binding.m_virtual_key >= VK_F1 && binding.m_virtual_key <= VK_F24)
  {
    text += L"F" + std::to_wstring(binding.m_virtual_key - VK_F1 + 1);
  }
  else
  {
    text += L"按键";
  }
  return text;
}

UINT currentModifiers() noexcept
{
  UINT modifiers = 0;
  if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
  {
    modifiers |= MOD_CONTROL;
  }
  if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
  {
    modifiers |= MOD_SHIFT;
  }
  if ((GetKeyState(VK_MENU) & 0x8000) != 0)
  {
    modifiers |= MOD_ALT;
  }
  if ((GetKeyState(VK_LWIN) & 0x8000) != 0 ||
      (GetKeyState(VK_RWIN) & 0x8000) != 0)
  {
    modifiers |= MOD_WIN;
  }
  return modifiers;
}

bool isModifierKey(UINT key) noexcept
{
  return key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU ||
         key == VK_LWIN || key == VK_RWIN;
}

}  // namespace

struct SettingsWindow::Impl
{
  explicit Impl(SettingsApplicationService& service_in)
      : service(service_in)
  {
  }

  SettingsApplicationService& service;
  std::function<void(const SettingsState&)> applied_callback;
  std::unique_ptr<SettingsWindowModel> model;
  HWND window{nullptr};
  UINT dpi{BaseDpi};
  FontHandle body_font;
  FontHandle title_font;
  FontHandle section_font;
  FontHandle caption_font;
  bool updating_controls{false};
  std::array<HWND, 3> navigation{};
  std::array<HWND, 2> general_controls{};
  std::array<HWND, 5> hotkey_controls{};
  std::array<HWND, 8> longshot_controls{};
  HWND restore_button{nullptr};
  HWND cancel_button{nullptr};
  HWND apply_button{nullptr};
  HWND banner{nullptr};
  HWND field_error{nullptr};
  HWND warning{nullptr};

  static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam,
                                     LPARAM lparam)
  {
    Impl* data = nullptr;
    if (message == WM_NCCREATE)
    {
      const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
      data = static_cast<Impl*>(create->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(data));
      if (data != nullptr)
      {
        data->window = hwnd;
      }
    }
    else
    {
      data = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (data == nullptr)
    {
      return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    return data->handleMessage(message, wparam, lparam);
  }

  bool buildControls()
  {
    body_font.create(10, FW_NORMAL, dpi);
    title_font.create(18, FW_SEMIBOLD, dpi);
    section_font.create(11, FW_SEMIBOLD, dpi);
    caption_font.create(9, FW_NORMAL, dpi);
    const auto create = [this](DWORD style, int id, const wchar_t* text)
    {
      HWND control = CreateWindowExW(
          0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
          window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
          GetModuleHandleW(nullptr), nullptr);
      if (control != nullptr)
      {
        SendMessageW(control, WM_SETFONT,
                     reinterpret_cast<WPARAM>(body_font.get()), TRUE);
      }
      return control;
    };
    navigation = {create(BS_OWNERDRAW | WS_TABSTOP, NavigationGeneral, L"常规"),
                  create(BS_OWNERDRAW | WS_TABSTOP, NavigationHotkeys, L"快捷键"),
                  create(BS_OWNERDRAW | WS_TABSTOP, NavigationLongShot, L"长截图")};
    general_controls = {
        create(BS_OWNERDRAW | WS_TABSTOP, AutostartCheck, L"开机时启动轻映"),
        create(BS_OWNERDRAW | WS_TABSTOP, AgentCheck, L"允许本机 Agent 接口")};
    hotkey_controls = {
        create(BS_OWNERDRAW | WS_TABSTOP, CaptureHotkeyButton, L""),
        create(BS_OWNERDRAW | WS_TABSTOP, CopyShortcutButton, L""),
        create(BS_OWNERDRAW | WS_TABSTOP, LongShotShortcutButton, L"")};
    const auto create_edit = [this](int id)
    {
      HWND edit = CreateWindowExW(0, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER |
                                      ES_RIGHT | ES_AUTOHSCROLL,
                                  0, 0, 0, 0, window,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                  GetModuleHandleW(nullptr), nullptr);
      if (edit != nullptr)
      {
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(body_font.get()),
                     TRUE);
      }
      return edit;
    };
    longshot_controls = {
        create_edit(MaxFramesEdit),
        create(BS_OWNERDRAW | WS_TABSTOP, MaxFramesDecrease, L"-"),
        create(BS_OWNERDRAW | WS_TABSTOP, MaxFramesIncrease, L"+"),
        create_edit(MaxHeightEdit),
        create(BS_OWNERDRAW | WS_TABSTOP, MaxHeightDecrease, L"-"),
        create(BS_OWNERDRAW | WS_TABSTOP, MaxHeightIncrease, L"+"),
        nullptr,
        nullptr};
    restore_button = create(BS_OWNERDRAW | WS_TABSTOP, RestoreDefaultsButton,
                            L"恢复当前页默认值");
    cancel_button = create(BS_OWNERDRAW | WS_TABSTOP, CancelButton, L"取消");
    apply_button = create(BS_OWNERDRAW | WS_TABSTOP, ApplyButton, L"应用");
    banner = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT, 0, 0, 0,
                             0, window,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(BannerLabel)),
                             GetModuleHandleW(nullptr), nullptr);
    field_error = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT, 0, 0,
                                  0, 0, window,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(FieldErrorLabel)),
                                  GetModuleHandleW(nullptr), nullptr);
    warning = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT, 0, 0, 0,
                              0, window,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(LongShotWarningLabel)),
                              GetModuleHandleW(nullptr), nullptr);
    for (HWND control : {banner, field_error, warning})
    {
      if (control != nullptr)
      {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(body_font.get()),
                     TRUE);
      }
    }
    return navigation[0] != nullptr && navigation[1] != nullptr &&
           navigation[2] != nullptr && general_controls[0] != nullptr &&
           general_controls[1] != nullptr && hotkey_controls[0] != nullptr &&
           hotkey_controls[1] != nullptr && hotkey_controls[2] != nullptr &&
           longshot_controls[0] != nullptr && longshot_controls[3] != nullptr &&
           restore_button != nullptr && cancel_button != nullptr &&
           apply_button != nullptr && banner != nullptr && field_error != nullptr &&
           warning != nullptr;
  }

  void layout()
  {
    if (window == nullptr)
    {
      return;
    }
    RECT client{};
    GetClientRect(window, &client);
    const int nav_width = scale(NavigationWidth, dpi);
    const int footer_height = scale(FooterHeight, dpi);
    const int padding = scale(ContentPadding, dpi);
    const int content_x = nav_width + padding;
    const int available_width = static_cast<int>(client.right) - content_x - padding;
    const int content_width = (std::max)(scale(200, dpi), available_width);
    const int footer_top = client.bottom - footer_height;
    for (std::size_t index = 0; index < navigation.size(); ++index)
    {
      MoveWindow(navigation[index], scale(18, dpi),
                 scale(104 + static_cast<int>(index) * 48, dpi),
                 nav_width - scale(36, dpi), scale(40, dpi), TRUE);
    }
    MoveWindow(general_controls[0],
               content_x + scale(GeneralCardContentInset, dpi),
               scale(GeneralFirstRowTop, dpi),
               content_width - scale(GeneralCardContentInset * 2, dpi),
               scale(GeneralRowHeight, dpi), TRUE);
    MoveWindow(general_controls[1],
               content_x + scale(GeneralCardContentInset, dpi),
               scale(GeneralSecondRowTop, dpi),
               content_width - scale(GeneralCardContentInset * 2, dpi),
               scale(GeneralRowHeight, dpi), TRUE);
    const int capsule_width = scale(150, dpi);
    const int capsule_x = content_x + content_width - capsule_width - scale(24, dpi);
    for (std::size_t index = 0; index < hotkey_controls.size(); ++index)
    {
      MoveWindow(hotkey_controls[index], capsule_x,
                 scale(150 + static_cast<int>(index) * HotkeyRowHeight, dpi),
                 capsule_width, scale(36, dpi), TRUE);
    }
    const int stepper_width = StepperValueWidth + StepperGap +
        StepperButtonSize + StepperGap + StepperButtonSize;
    const int stepper_x = content_x + content_width - scale(24, dpi) -
        scale(stepper_width, dpi);
    const int stepper_value_width = scale(StepperValueWidth, dpi);
    const int stepper_button_size = scale(StepperButtonSize, dpi);
    const int stepper_gap = scale(StepperGap, dpi);
    const int stepper_first_y = scale(LongShotFirstRowTop +
                                      LongShotControlTopOffset, dpi);
    const int stepper_second_y = scale(LongShotSecondRowTop +
                                       LongShotControlTopOffset, dpi);
    MoveWindow(longshot_controls[0], stepper_x, stepper_first_y,
               stepper_value_width, scale(LongShotControlHeight, dpi), TRUE);
    MoveWindow(longshot_controls[1], stepper_x + stepper_value_width + stepper_gap,
               stepper_first_y, stepper_button_size, stepper_button_size, TRUE);
    MoveWindow(longshot_controls[2], stepper_x + stepper_value_width + stepper_gap +
                   stepper_button_size + stepper_gap,
               stepper_first_y, stepper_button_size, stepper_button_size, TRUE);
    MoveWindow(longshot_controls[3], stepper_x, stepper_second_y,
               stepper_value_width, scale(LongShotControlHeight, dpi), TRUE);
    MoveWindow(longshot_controls[4], stepper_x + stepper_value_width + stepper_gap,
               stepper_second_y, stepper_button_size, stepper_button_size, TRUE);
    MoveWindow(longshot_controls[5], stepper_x + stepper_value_width + stepper_gap +
                   stepper_button_size + stepper_gap,
               stepper_second_y, stepper_button_size, stepper_button_size, TRUE);
    MoveWindow(banner, content_x, scale(100, dpi), content_width, scale(24, dpi), TRUE);
    MoveWindow(field_error, content_x, scale(100, dpi), content_width,
               scale(28, dpi), TRUE);
    MoveWindow(warning, content_x, scale(306, dpi), content_width,
               scale(44, dpi), TRUE);
    MoveWindow(restore_button, scale(24, dpi), footer_top + scale(20, dpi),
               scale(148, dpi), scale(36, dpi), TRUE);
    MoveWindow(apply_button, client.right - scale(28, dpi) - scale(84, dpi),
               footer_top + scale(20, dpi), scale(84, dpi), scale(36, dpi), TRUE);
    MoveWindow(cancel_button, client.right - scale(124, dpi) - scale(84, dpi),
               footer_top + scale(20, dpi), scale(84, dpi), scale(36, dpi), TRUE);
  }

  void drawNavigationItem(const DRAWITEMSTRUCT& item)
  {
    const SettingsPage page = model == nullptr ? SettingsPage::General
                                                : model->currentPage();
    const bool selected = static_cast<int>(item.CtlID) - NavigationGeneral ==
        static_cast<int>(page);
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const COLORREF fill_color = selected ? SelectedNavigationColor
        : pressed ? RGB(239, 242, 247) : NavigationBackgroundColor;
    drawRoundedRectangle(item.hDC, item.rcItem, scale(8, dpi), fill_color,
                         fill_color);
    if (selected)
    {
      BrushHandle indicator(PrimaryColor);
      RECT indicator_rect{item.rcItem.left, item.rcItem.top + scale(10, dpi),
                          item.rcItem.left + scale(3, dpi),
                          item.rcItem.bottom - scale(10, dpi)};
      FillRect(item.hDC, &indicator_rect, indicator.get());
    }
    RECT text_rect{item.rcItem.left + scale(16, dpi), item.rcItem.top,
                   item.rcItem.right - scale(12, dpi), item.rcItem.bottom};
    drawText(item.hDC, text_rect, controlText(item.hwndItem), body_font.get(),
             selected ? PrimaryColor : PrimaryTextColor, DT_LEFT);
  }

  void drawHotkeyInput(const DRAWITEMSTRUCT& item)
  {
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool hovered = (item.itemState & ODS_HOTLIGHT) != 0;
    const SettingsShortcutField field = item.CtlID == CaptureHotkeyButton
        ? SettingsShortcutField::Capture
        : item.CtlID == CopyShortcutButton ? SettingsShortcutField::Copy
                                            : SettingsShortcutField::ToggleLongShot;
    const bool recording = model != nullptr &&
        model->recordingShortcut() == field;
    const COLORREF fill_color = recording ? RGB(245, 249, 255)
        : pressed || hovered ? ControlHoverColor : ContentBackgroundColor;
    const COLORREF border_color = recording ? PrimaryColor : ControlBorderColor;
    drawRoundedRectangle(item.hDC, item.rcItem, scale(HotkeyInputRadius, dpi), fill_color,
                         border_color);
    RECT text_rect{item.rcItem.left + scale(10, dpi), item.rcItem.top,
                   item.rcItem.right - scale(10, dpi), item.rcItem.bottom};
    drawText(item.hDC, text_rect, controlText(item.hwndItem), body_font.get(),
             recording ? PrimaryColor : PrimaryTextColor, DT_CENTER);
  }

  void drawGeneralSettingRow(const DRAWITEMSTRUCT& item)
  {
    const bool autostart = item.CtlID == AutostartCheck;
    const bool checked = model != nullptr &&
        (autostart ? model->draft().m_autostart_enabled
                   : model->draft().m_agent_enabled);
    const bool focused = (item.itemState & ODS_FOCUS) != 0;
    BrushHandle row_background(ContentBackgroundColor);
    FillRect(item.hDC, &item.rcItem, row_background.get());

    const int checkbox_size = scale(GeneralCheckboxSize, dpi);
    const int checkbox_top = item.rcItem.top +
        (item.rcItem.bottom - item.rcItem.top - checkbox_size) / 2;
    const RECT checkbox_rect{item.rcItem.left, checkbox_top,
                             item.rcItem.left + checkbox_size,
                             checkbox_top + checkbox_size};
    drawRoundedRectangle(item.hDC, checkbox_rect,
                         scale(GeneralCheckboxRadius, dpi),
                         checked ? PrimaryColor : ContentBackgroundColor,
                         checked ? PrimaryColor
                                 : focused ? FocusBorderColor : BorderColor);
    if (checked)
    {
      POINT checkmark[GeneralCheckmarkPointCount]{};
      checkmark[0].x = checkbox_rect.left + scale(4, dpi);
      checkmark[0].y = checkbox_rect.top + scale(10, dpi);
      checkmark[1].x = checkbox_rect.left + scale(8, dpi);
      checkmark[1].y = checkbox_rect.top + scale(14, dpi);
      checkmark[2].x = checkbox_rect.left + scale(16, dpi);
      checkmark[2].y = checkbox_rect.top + scale(6, dpi);
      PenHandle checkmark_pen(ContentBackgroundColor,
                              scale(GeneralCheckmarkStroke, dpi));
      if (checkmark_pen.get() != nullptr)
      {
        const HGDIOBJ previous_pen = SelectObject(item.hDC, checkmark_pen.get());
        Polyline(item.hDC, checkmark, GeneralCheckmarkPointCount);
        if (previous_pen != nullptr)
        {
          SelectObject(item.hDC, previous_pen);
        }
      }
    }

    const int text_left = checkbox_rect.right + scale(GeneralCheckboxTextGap, dpi);
    drawText(item.hDC,
             RECT{text_left, item.rcItem.top + scale(2, dpi), item.rcItem.right,
                  item.rcItem.top + scale(26, dpi)},
             controlText(item.hwndItem), section_font.get(), PrimaryTextColor,
             DT_LEFT);
    drawText(item.hDC,
             RECT{text_left, item.rcItem.top + scale(27, dpi), item.rcItem.right,
                  item.rcItem.bottom},
             autostart ? L"Windows 启动时自动运行轻映"
                       : L"轻映 · Windows 原生轻量截图工具",
             caption_font.get(), SecondaryTextColor, DT_LEFT);
  }

  void drawFooterButton(const DRAWITEMSTRUCT& item, bool primary)
  {
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const COLORREF fill_color = primary
        ? disabled ? RGB(191, 219, 254)
                   : pressed ? PrimaryHoverColor : PrimaryColor
        : pressed ? RGB(243, 244, 246) : ContentBackgroundColor;
    const COLORREF border_color = primary ? fill_color : BorderColor;
    drawRoundedRectangle(item.hDC, item.rcItem, scale(8, dpi), fill_color,
                         border_color);
    drawText(item.hDC, item.rcItem, controlText(item.hwndItem), body_font.get(),
             primary ? RGB(255, 255, 255) : SecondaryTextColor, DT_CENTER);
  }

  void drawSmallButton(const DRAWITEMSTRUCT& item)
  {
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    drawRoundedRectangle(item.hDC, item.rcItem, scale(6, dpi),
                         pressed ? RGB(243, 246, 251) : ContentBackgroundColor,
                         BorderColor);
    drawText(item.hDC, item.rcItem, controlText(item.hwndItem), section_font.get(),
             SecondaryTextColor, DT_CENTER);
  }

  void drawStepperButton(const DRAWITEMSTRUCT& item)
  {
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool hovered = (item.itemState & ODS_HOTLIGHT) != 0;
    const COLORREF fill_color = pressed || hovered ? ControlHoverColor
                                                   : ContentBackgroundColor;
    drawRoundedRectangle(item.hDC, item.rcItem, scale(StepperButtonRadius, dpi),
                         fill_color, ControlBorderColor);
    drawText(item.hDC, item.rcItem, controlText(item.hwndItem), body_font.get(),
             PrimaryTextColor, DT_CENTER);
  }

  void drawOwnerDrawItem(const DRAWITEMSTRUCT& item)
  {
    if (item.CtlID >= NavigationGeneral && item.CtlID <= NavigationLongShot)
    {
      drawNavigationItem(item);
      return;
    }
    if (item.CtlID == AutostartCheck || item.CtlID == AgentCheck)
    {
      drawGeneralSettingRow(item);
      return;
    }
    if (item.CtlID >= CaptureHotkeyButton &&
        item.CtlID <= LongShotShortcutButton)
    {
      drawHotkeyInput(item);
      return;
    }
    if (item.CtlID == ApplyButton)
    {
      drawFooterButton(item, true);
      return;
    }
    if (item.CtlID == CancelButton || item.CtlID == RestoreDefaultsButton)
    {
      drawFooterButton(item, false);
      return;
    }
    if (item.CtlID >= MaxFramesDecrease && item.CtlID <= MaxHeightIncrease)
    {
      drawStepperButton(item);
      return;
    }
    drawSmallButton(item);
  }

  void refreshControls()
  {
    if (model == nullptr)
    {
      return;
    }
    updating_controls = true;
    const SettingsDraft& draft = model->draft();
    SetWindowTextW(hotkey_controls[0], model->recordingShortcut() ==
            SettingsShortcutField::Capture ? L"请按下新快捷键" :
            shortcutText(draft.m_settings.m_capture_hotkey).c_str());
    SetWindowTextW(hotkey_controls[1], model->recordingShortcut() ==
            SettingsShortcutField::Copy ? L"请按下新快捷键" :
            shortcutText(draft.m_settings.m_selection_shortcuts.m_copy).c_str());
    SetWindowTextW(hotkey_controls[2], model->recordingShortcut() ==
            SettingsShortcutField::ToggleLongShot ? L"请按下新快捷键" :
            shortcutText(draft.m_settings.m_selection_shortcuts.m_toggle_longshot).c_str());
    SetWindowTextW(longshot_controls[0],
                   std::to_wstring(draft.m_settings.m_longshot_limits.max_frames).c_str());
    SetWindowTextW(longshot_controls[3], std::to_wstring(
                       draft.m_settings.m_longshot_limits.max_output_height).c_str());
    const SettingsPage page = model->currentPage();
    for (std::size_t index = 0; index < navigation.size(); ++index)
    {
      const wchar_t* label = index == 0 ? L"常规" : index == 1 ? L"快捷键" : L"长截图";
      SetWindowTextW(navigation[index], label);
    }
    SetWindowTextW(restore_button,
                   page == SettingsPage::Hotkeys ? L"恢复默认快捷键"
                                                   : L"恢复当前页默认值");
    for (HWND control : general_controls)
    {
      ShowWindow(control, page == SettingsPage::General ? SW_SHOW : SW_HIDE);
    }
    for (HWND control : hotkey_controls)
    {
      ShowWindow(control, page == SettingsPage::Hotkeys ? SW_SHOW : SW_HIDE);
    }
    for (std::size_t index = 0; index < 6; ++index)
    {
      ShowWindow(longshot_controls[index], page == SettingsPage::LongShot ? SW_SHOW : SW_HIDE);
    }
    SettingsFieldError active_error = SettingsFieldError::None;
    if (page == SettingsPage::Hotkeys)
    {
      constexpr std::array<SettingsWindowField, 3> hotkey_fields{
          SettingsWindowField::CaptureHotkey,
          SettingsWindowField::CopyShortcut,
          SettingsWindowField::ToggleLongShotShortcut};
      for (const SettingsWindowField field : hotkey_fields)
      {
        active_error = model->fieldError(field);
        if (active_error != SettingsFieldError::None)
        {
          break;
        }
      }
    }
    else if (page == SettingsPage::LongShot)
    {
      active_error = model->fieldError(SettingsWindowField::LongShotLimits);
    }
    std::wstring error = settingsFieldErrorMessage(active_error);
    SetWindowTextW(field_error, error.c_str());
    ShowWindow(field_error, error.empty() ? SW_HIDE : SW_SHOW);
    std::wstring banner_text = model->bannerMessage();
    SetWindowTextW(banner, banner_text.c_str());
    ShowWindow(banner, !error.empty() || banner_text.empty() ? SW_HIDE : SW_SHOW);
    std::wstring warning_text;
    if (page == SettingsPage::LongShot && model->shouldWarnLongShotDuration())
    {
      warning_text += L"超过 30 帧可能延长截取时间。";
    }
    if (page == SettingsPage::LongShot && model->shouldWarnLongShotMemory())
    {
      if (!warning_text.empty())
      {
        warning_text += L" ";
      }
      warning_text += L"超过 30000 像素可能占用较多内存。";
    }
    SetWindowTextW(warning, warning_text.c_str());
    ShowWindow(warning, warning_text.empty() ? SW_HIDE : SW_SHOW);
    EnableWindow(apply_button, model->canUseApplyButton() ? TRUE : FALSE);
    updating_controls = false;
    InvalidateRect(window, nullptr, TRUE);
  }

  void updateNumericControl(int control_id)
  {
    if (updating_controls || model == nullptr)
    {
      return;
    }
    wchar_t text[32]{};
    GetWindowTextW(GetDlgItem(window, control_id), text, 32);
    wchar_t* end = nullptr;
    const long value = std::wcstol(text, &end, 10);
    if (end == text)
    {
      if (control_id == MaxFramesEdit)
      {
        model->setLongShotMaxFrames(0);
      }
      else
      {
        model->setLongShotMaxOutputHeight(0);
      }
    }
    else if (control_id == MaxFramesEdit)
    {
      model->setLongShotMaxFrames(static_cast<int>(value));
    }
    else
    {
      model->setLongShotMaxOutputHeight(static_cast<int>(value));
    }
    refreshControls();
  }

  bool apply()
  {
    if (model == nullptr || !model->canUseApplyButton())
    {
      return false;
    }
    if (!model->dirty())
    {
      model->recordNoChangesApply();
      refreshControls();
      return true;
    }
    const SettingsApplyResult result = service.apply(model->draft());
    model->recordApplyResult(result);
    if (!result.m_committed)
    {
      focusFirstFieldError(result.m_field_errors);
    }
    if (result.m_committed && applied_callback)
    {
      applied_callback(result.m_state);
    }
    refreshControls();
    return result.m_committed;
  }

  void focusFirstFieldError(const SettingsFieldErrors& errors) noexcept
  {
    HWND control = nullptr;
    if (errors.m_capture_hotkey != SettingsFieldError::None)
    {
      control = hotkey_controls[0];
    }
    else if (errors.m_copy_shortcut != SettingsFieldError::None)
    {
      control = hotkey_controls[1];
    }
    else if (errors.m_toggle_longshot_shortcut != SettingsFieldError::None)
    {
      control = hotkey_controls[2];
    }
    else if (errors.m_longshot_limits != SettingsFieldError::None)
    {
      control = longshot_controls[0];
    }
    if (control != nullptr)
    {
      SetFocus(control);
    }
  }

  bool requestClose()
  {
    if (model != nullptr && model->dirty())
    {
      const int choice = MessageBoxW(
          window, L"存在未应用的设置。是否应用后关闭？", L"轻映设置",
          MB_YESNOCANCEL | MB_ICONQUESTION);
      if (choice == IDCANCEL)
      {
        return false;
      }
      if (choice == IDYES && !apply())
      {
        return false;
      }
    }
    DestroyWindow(window);
    return true;
  }

  void handleShortcutKey(UINT key)
  {
    if (model == nullptr)
    {
      return;
    }
    if (model->recordingShortcut().has_value())
    {
      if (key == VK_ESCAPE)
      {
        model->cancelShortcutRecording();
      }
      else if (key == VK_BACK)
      {
        static_cast<void>(model->clearShortcut(*model->recordingShortcut()));
        model->cancelShortcutRecording();
      }
      else if (!isModifierKey(key))
      {
        static_cast<void>(model->recordShortcut(
            ShortcutBinding{currentModifiers(), key}));
      }
      refreshControls();
      return;
    }
    if (key == VK_ESCAPE)
    {
      static_cast<void>(requestClose());
      return;
    }
    if (key == VK_RETURN)
    {
      static_cast<void>(apply());
      return;
    }
    if (key == VK_LEFT || key == VK_RIGHT)
    {
      const int page = static_cast<int>(model->currentPage());
      const int delta = key == VK_LEFT ? -1 : 1;
      const int next = (page + delta + 3) % 3;
      model->selectPage(static_cast<SettingsPage>(next));
      refreshControls();
    }
  }

  LRESULT handleMessage(UINT message, WPARAM wparam, LPARAM lparam)
  {
    switch (message)
    {
      case WM_CREATE:
        if (!buildControls())
        {
          return -1;
        }
        layout();
        refreshControls();
        return 0;
      case WM_GETMINMAXINFO:
      {
        auto* minmax = reinterpret_cast<MINMAXINFO*>(lparam);
        minmax->ptMinTrackSize.x = scale(MinimumWidth, dpi);
        minmax->ptMinTrackSize.y = scale(MinimumHeight, dpi);
        return 0;
      }
      case WM_DPICHANGED:
      {
        dpi = HIWORD(wparam);
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        body_font.create(10, FW_NORMAL, dpi);
        title_font.create(18, FW_SEMIBOLD, dpi);
        section_font.create(11, FW_SEMIBOLD, dpi);
        caption_font.create(9, FW_NORMAL, dpi);
        layout();
        refreshControls();
        return 0;
      }
      case WM_SIZE:
        layout();
        return 0;
      case WM_COMMAND:
      {
        const int control_id = LOWORD(wparam);
        const int notification = HIWORD(wparam);
        if (control_id == NavigationGeneral || control_id == NavigationHotkeys ||
            control_id == NavigationLongShot)
        {
          model->selectPage(static_cast<SettingsPage>(control_id - NavigationGeneral));
          refreshControls();
          return 0;
        }
        if (control_id == AutostartCheck && notification == BN_CLICKED)
        {
          model->toggleAutostart();
          refreshControls();
          return 0;
        }
        if (control_id == AgentCheck && notification == BN_CLICKED)
        {
          model->toggleAgent();
          refreshControls();
          return 0;
        }
        if ((control_id == CaptureHotkeyButton || control_id == CopyShortcutButton ||
             control_id == LongShotShortcutButton) && notification == BN_CLICKED)
        {
          const SettingsShortcutField field = control_id == CaptureHotkeyButton
              ? SettingsShortcutField::Capture
              : control_id == CopyShortcutButton ? SettingsShortcutField::Copy
                                                  : SettingsShortcutField::ToggleLongShot;
          static_cast<void>(model->beginShortcutRecording(field));
          SetFocus(window);
          refreshControls();
          return 0;
        }
        if ((control_id == MaxFramesEdit || control_id == MaxHeightEdit) &&
            notification == EN_CHANGE)
        {
          updateNumericControl(control_id);
          return 0;
        }
        if (control_id == MaxFramesDecrease || control_id == MaxFramesIncrease ||
            control_id == MaxHeightDecrease || control_id == MaxHeightIncrease)
        {
          const int frame_delta = control_id == MaxFramesDecrease ? -5
              : control_id == MaxFramesIncrease ? 5 : 0;
          const int height_delta = control_id == MaxHeightDecrease ? -5000
              : control_id == MaxHeightIncrease ? 5000 : 0;
          if (frame_delta != 0)
          {
            model->setLongShotMaxFrames(
                model->draft().m_settings.m_longshot_limits.max_frames + frame_delta);
          }
          else
          {
            model->setLongShotMaxOutputHeight(
                model->draft().m_settings.m_longshot_limits.max_output_height + height_delta);
          }
          refreshControls();
          return 0;
        }
        if (control_id == RestoreDefaultsButton)
        {
          model->restoreCurrentPageDefaults();
          refreshControls();
          return 0;
        }
        if (control_id == ApplyButton)
        {
          static_cast<void>(apply());
          return 0;
        }
        if (control_id == CancelButton)
        {
          static_cast<void>(requestClose());
          return 0;
        }
        return 0;
      }
      case WM_KEYDOWN:
        handleShortcutKey(static_cast<UINT>(wparam));
        return 0;
      case WM_DRAWITEM:
        drawOwnerDrawItem(*reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
        return TRUE;
      case WM_CTLCOLORSTATIC:
      {
        HDC dc = reinterpret_cast<HDC>(wparam);
        const HWND control = reinterpret_cast<HWND>(lparam);
        SetBkMode(dc, TRANSPARENT);
        if (control == field_error)
        {
          SetTextColor(dc, ErrorTextColor);
        }
        else if (control == banner)
        {
          SetTextColor(dc, StatusTextColor);
        }
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
      }
      case WM_CTLCOLOREDIT:
      {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, ContentBackgroundColor);
        SetTextColor(dc, PrimaryTextColor);
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
      }
      case WM_PAINT:
      {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        const int nav_width = scale(NavigationWidth, dpi);
        const int footer_top = client.bottom - scale(FooterHeight, dpi);
        const int content_x = nav_width + scale(ContentPadding, dpi);
        const int content_width = client.right - content_x - scale(ContentPadding, dpi);
        BrushHandle content_brush(PageBackgroundColor);
        BrushHandle nav_brush(NavigationBackgroundColor);
        BrushHandle footer_brush(ContentBackgroundColor);
        RECT navigation_rect{0, 0, nav_width, footer_top};
        RECT content_rect{nav_width, 0, client.right, footer_top};
        RECT footer_rect{0, footer_top, client.right, client.bottom};
        FillRect(dc, &navigation_rect, nav_brush.get());
        FillRect(dc, &content_rect, content_brush.get());
        FillRect(dc, &footer_rect, footer_brush.get());
        PenHandle footer_pen(BorderColor);
        const HGDIOBJ previous_pen = SelectObject(dc, footer_pen.get());
        MoveToEx(dc, 0, footer_top, nullptr);
        LineTo(dc, client.right, footer_top);
        SelectObject(dc, previous_pen);
        drawText(dc, RECT{scale(24, dpi), scale(26, dpi), nav_width - scale(20, dpi),
                          scale(58, dpi)},
                 L"轻映设置", section_font.get(), PrimaryTextColor, DT_LEFT);
        const SettingsPage page = model == nullptr ? SettingsPage::General : model->currentPage();
        const wchar_t* title = page == SettingsPage::General ? L"常规"
            : page == SettingsPage::Hotkeys ? L"快捷键" : L"长截图";
        drawText(dc, RECT{content_x, scale(30, dpi), content_x + content_width,
                          scale(62, dpi)},
                 title, title_font.get(), PrimaryTextColor, DT_LEFT);
        const wchar_t* description = page == SettingsPage::General
            ? L"管理轻映的启动方式和本机接口。"
            : page == SettingsPage::Hotkeys
                ? L"自定义轻映常用操作的快捷键。"
                : L"设置下一次长截图的安全上限。";
        drawText(dc, RECT{content_x, scale(70, dpi), content_x + content_width,
                          scale(94, dpi)},
                 description, body_font.get(), SecondaryTextColor, DT_LEFT);
        const int card_bottom = page == SettingsPage::Hotkeys ? scale(358, dpi)
            : page == SettingsPage::General ? scale(GeneralCardBottom, dpi)
                                             : scale(270, dpi);
        RECT card_rect{content_x, scale(132, dpi), content_x + content_width,
                       card_bottom};
        drawRoundedRectangle(dc, card_rect, scale(10, dpi), ContentBackgroundColor,
                             ContentCardBorderColor);
        if (page == SettingsPage::General)
        {
          PenHandle separator_pen(SettingsRowDividerColor);
          const HGDIOBJ previous_separator = SelectObject(dc, separator_pen.get());
          MoveToEx(dc, content_x + scale(GeneralCardContentInset, dpi),
                   scale(GeneralRowDividerY, dpi), nullptr);
          LineTo(dc, content_x + content_width -
                     scale(GeneralCardContentInset, dpi),
                 scale(GeneralRowDividerY, dpi));
          if (previous_separator != nullptr)
          {
            SelectObject(dc, previous_separator);
          }
        }
        if (page == SettingsPage::Hotkeys)
        {
          constexpr std::array<const wchar_t*, 3> labels{
              L"开始截图", L"复制截图", L"长截图控制"};
          constexpr std::array<const wchar_t*, 3> details{
              L"开启选区并进行截图", L"将当前截图复制到剪贴板", L"开始或结束长截图"};
          for (int index = 0; index < 3; ++index)
          {
            const int row_top = scale(HotkeyRowTop + index * HotkeyRowHeight, dpi);
            drawText(dc, RECT{content_x + scale(24, dpi), row_top,
                              content_x + scale(190, dpi), row_top + scale(26, dpi)},
                     labels[static_cast<std::size_t>(index)], body_font.get(),
                     PrimaryTextColor, DT_LEFT);
            drawText(dc, RECT{content_x + scale(24, dpi), row_top + scale(26, dpi),
                              content_x + scale(210, dpi), row_top + scale(48, dpi)},
                     details[static_cast<std::size_t>(index)], caption_font.get(),
                     HelperTextColor, DT_LEFT);
            if (index < 2)
            {
              PenHandle separator_pen(SettingsRowDividerColor);
              const HGDIOBJ previous_separator = SelectObject(dc, separator_pen.get());
              MoveToEx(dc, content_x + scale(24, dpi),
                       row_top + scale(HotkeyRowDividerOffset, dpi), nullptr);
              LineTo(dc, content_x + content_width - scale(24, dpi),
                     row_top + scale(HotkeyRowDividerOffset, dpi));
              SelectObject(dc, previous_separator);
            }
          }
        }
        if (page == SettingsPage::LongShot)
        {
          drawText(dc, RECT{content_x + scale(24, dpi),
                            scale(LongShotFirstRowTop, dpi),
                            content_x + scale(300, dpi),
                            scale(LongShotFirstRowTop + 24, dpi)},
                   L"最大帧数", body_font.get(), PrimaryTextColor, DT_LEFT);
          drawText(dc, RECT{content_x + scale(24, dpi),
                            scale(LongShotSecondRowTop, dpi),
                            content_x + scale(300, dpi),
                            scale(LongShotSecondRowTop + 24, dpi)},
                   L"最大输出高度", body_font.get(), PrimaryTextColor, DT_LEFT);
          drawText(dc, RECT{content_x + scale(24, dpi),
                            scale(LongShotFirstRowTop + 24, dpi),
                            content_x + scale(340, dpi),
                            scale(LongShotFirstRowTop + LongShotTextBlockHeight, dpi)},
                   L"单次长截图允许的最大帧数", caption_font.get(),
                   HelperTextColor, DT_LEFT);
          drawText(dc, RECT{content_x + scale(24, dpi),
                            scale(LongShotSecondRowTop + 24, dpi),
                            content_x + scale(360, dpi),
                            scale(LongShotSecondRowTop + LongShotTextBlockHeight, dpi)},
                   L"长截图生成的最大高度（像素）", caption_font.get(),
                   HelperTextColor, DT_LEFT);
          PenHandle separator_pen(SettingsRowDividerColor);
          const HGDIOBJ previous_separator = SelectObject(dc, separator_pen.get());
          MoveToEx(dc, content_x + scale(24, dpi), scale(LongShotRowDividerY, dpi), nullptr);
          LineTo(dc, content_x + content_width - scale(24, dpi),
                 scale(LongShotRowDividerY, dpi));
          SelectObject(dc, previous_separator);
        }
        EndPaint(window, &paint);
        return 0;
      }
      case WM_ERASEBKGND:
        return 1;
      case WM_CLOSE:
        static_cast<void>(requestClose());
        return 0;
      case WM_NCDESTROY:
      {
        const HWND destroyed_window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        window = nullptr;
        return DefWindowProcW(destroyed_window, message, wparam, lparam);
      }
      default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
  }
};

SettingsWindow::SettingsWindow(SettingsApplicationService& service)
    : m_impl(std::make_unique<Impl>(service))
{
}

SettingsWindow::~SettingsWindow()
{
  if (visible())
  {
    DestroyWindow(m_impl->window);
  }
}

bool SettingsWindow::show(HWND owner, const SettingsState& initial_state)
{
  if (visible())
  {
    refreshExternalState(initial_state);
    activate();
    return true;
  }
  static std::once_flag registration_once;
  static bool class_registered = false;
  std::call_once(registration_once, []
  {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const HICON icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_QINGYING));
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = Impl::windowProc;
    window_class.hInstance = instance;
    window_class.hIcon = icon;
    window_class.hIconSm = icon;
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    window_class.lpszClassName = SettingsWindowClassName;
    class_registered = RegisterClassExW(&window_class) != 0 ||
        GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  });
  if (!class_registered)
  {
    return false;
  }
  m_impl->model = std::make_unique<SettingsWindowModel>(initial_state);
  const UINT dpi = owner == nullptr ? BaseDpi : GetDpiForWindow(owner);
  m_impl->dpi = dpi == 0 ? BaseDpi : dpi;
  const int width = scale(DefaultWidth, m_impl->dpi);
  const int height = scale(DefaultHeight, m_impl->dpi);
  RECT work_area{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
  MONITORINFO monitor_info{};
  monitor_info.cbSize = sizeof(monitor_info);
  if (GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor_info))
  {
    work_area = monitor_info.rcWork;
  }
  const int x = work_area.left + ((work_area.right - work_area.left) - width) / 2;
  const int y = work_area.top + ((work_area.bottom - work_area.top) - height) / 2;
  const HWND window = CreateWindowExW(
      WS_EX_APPWINDOW, SettingsWindowClassName, L"轻映设置",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME,
      x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr), m_impl.get());
  if (window == nullptr)
  {
    m_impl->model.reset();
    return false;
  }
  ShowWindow(window, SW_SHOWNORMAL);
  UpdateWindow(window);
  return true;
}

void SettingsWindow::activate() noexcept
{
  if (!visible())
  {
    return;
  }
  ShowWindow(m_impl->window, SW_RESTORE);
  SetForegroundWindow(m_impl->window);
}

void SettingsWindow::close() noexcept
{
  if (visible())
  {
    DestroyWindow(m_impl->window);
  }
}

void SettingsWindow::refreshExternalState(const SettingsState& state)
{
  if (m_impl->model == nullptr)
  {
    return;
  }
  m_impl->model->refreshExternalState(state);
  if (visible())
  {
    m_impl->refreshControls();
  }
}

void SettingsWindow::setAppliedCallback(
    std::function<void(const SettingsState&)> callback)
{
  m_impl->applied_callback = std::move(callback);
}

bool SettingsWindow::visible() const noexcept
{
  return m_impl->window != nullptr && IsWindow(m_impl->window) != FALSE;
}

}  // namespace qingying
