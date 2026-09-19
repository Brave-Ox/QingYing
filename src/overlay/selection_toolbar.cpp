#include "qingying/overlay/selection_toolbar.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <utility>

#include "qingying/ui/modern_toolbar.hpp"

namespace qingying {

namespace {

bool isRecoveryPhase(OverlayPhase phase) noexcept {
  return phase == OverlayPhase::LongShotRecoverable ||
         phase == OverlayPhase::LongShotResultPending;
}

SelectionToolbarItems buildRecoveryItems(
    LongShotRecoveryState recovery) noexcept {
  SelectionToolbarCommand accept_command =
      SelectionToolbarCommand::KeepLongShotFrame;
  if (recovery.result == LongShotRecoveryResult::PartialResult) {
    accept_command = SelectionToolbarCommand::AcceptLongShotPartial;
  }
  const bool has_result = recovery.hasResult();
  return {{{SelectionToolbarCommand::RetryLongShot,
            SelectionToolbarIcon::LongShot, true},
           {SelectionToolbarCommand::AdjustLongShotSelection,
            SelectionToolbarIcon::Edit, true},
           {accept_command, SelectionToolbarIcon::Save, has_result},
           {SelectionToolbarCommand::Copy, SelectionToolbarIcon::Copy,
            false},
           {SelectionToolbarCommand::Pin, SelectionToolbarIcon::Pin, false},
           {SelectionToolbarCommand::CancelLongShot,
            SelectionToolbarIcon::Stop, true}}};
}

}  // namespace

SelectionToolbarItems buildSelectionToolbarItems(
    OverlayPhase phase, LongShotRecoveryState recovery) noexcept {
  if (isRecoveryPhase(phase)) {
    return buildRecoveryItems(recovery);
  }
  const bool result_actions_enabled =
      phase == OverlayPhase::Selected || phase == OverlayPhase::LongShotPaused;
  const bool toggle_enabled = phase == OverlayPhase::Selected ||
                              phase == OverlayPhase::LongShotRunning ||
                              phase == OverlayPhase::LongShotPaused;
  const bool stop_enabled = phase == OverlayPhase::LongShotRunning ||
                            phase == OverlayPhase::LongShotPaused;

  SelectionToolbarIcon toggle_icon = SelectionToolbarIcon::LongShot;
  if (phase == OverlayPhase::LongShotRunning ||
      phase == OverlayPhase::LongShotFinishing) {
    toggle_icon = SelectionToolbarIcon::Pause;
  } else if (phase == OverlayPhase::LongShotPaused) {
    toggle_icon = SelectionToolbarIcon::Resume;
  }

  return {{{SelectionToolbarCommand::Copy, SelectionToolbarIcon::Copy,
            result_actions_enabled},
           {SelectionToolbarCommand::Save, SelectionToolbarIcon::Save,
            result_actions_enabled},
           {SelectionToolbarCommand::ToggleLongShot, toggle_icon,
            toggle_enabled},
           {SelectionToolbarCommand::Edit, SelectionToolbarIcon::Edit,
            result_actions_enabled},
           {SelectionToolbarCommand::Pin, SelectionToolbarIcon::Pin,
            result_actions_enabled},
           {SelectionToolbarCommand::StopLongShot, SelectionToolbarIcon::Stop,
            stop_enabled}}};
}

SelectionToolbarStatus selectionToolbarStatus(
    OverlayPhase phase, LongShotRecoveryState recovery) noexcept {
  switch (phase) {
    case OverlayPhase::LongShotRunning:
      return SelectionToolbarStatus::Running;
    case OverlayPhase::LongShotPaused:
      return SelectionToolbarStatus::Paused;
    case OverlayPhase::LongShotFinishing:
      return SelectionToolbarStatus::Finishing;
    case OverlayPhase::LongShotRecoverable:
      return SelectionToolbarStatus::Recoverable;
    case OverlayPhase::LongShotResultPending:
      return recovery.result == LongShotRecoveryResult::PartialResult
                 ? SelectionToolbarStatus::PartialResultPending
                 : SelectionToolbarStatus::SingleFramePending;
    case OverlayPhase::Sniffing:
    case OverlayPhase::Creating:
    case OverlayPhase::Selected:
    case OverlayPhase::Closing:
      return SelectionToolbarStatus::None;
  }
  return SelectionToolbarStatus::None;
}

bool selectionToolbarShortcutCommand(
    OverlayPhase phase, const SelectionShortcutSettings& shortcuts,
    const ShortcutBinding& shortcut,
    SelectionToolbarCommand& command) noexcept
{
  SelectionToolbarCommand requested = SelectionToolbarCommand::Cancel;
  if (!shortcuts.m_copy.empty() && shortcut == shortcuts.m_copy)
  {
    requested = SelectionToolbarCommand::Copy;
  }
  else if (!shortcuts.m_toggle_longshot.empty() &&
           shortcut == shortcuts.m_toggle_longshot)
  {
    requested = SelectionToolbarCommand::ToggleLongShot;
  }
  else
  {
    return false;
  }

  const SelectionToolbarItems items = buildSelectionToolbarItems(phase);
  for (const SelectionToolbarItemModel& item : items)
  {
    if (item.command == requested && item.enabled)
    {
      command = requested;
      return true;
    }
  }
  return false;
}

bool selectionToolbarHotkeyCommand(OverlayPhase phase,
                                   const SelectionShortcutSettings& shortcuts,
                                   int hotkey_id,
                                   SelectionToolbarCommand& command) noexcept
{
  if (hotkey_id == SelectionToolbarCopyHotkeyId)
  {
    return selectionToolbarShortcutCommand(phase, shortcuts, shortcuts.m_copy,
                                           command);
  }
  if (hotkey_id == SelectionToolbarLongShotHotkeyId)
  {
    return selectionToolbarShortcutCommand(
        phase, shortcuts, shortcuts.m_toggle_longshot, command);
  }
  return false;
}

namespace {

constexpr int kToolbarDividerPadExtraPx = 6;
constexpr int kStatusRowHeight = 30;
constexpr int kStatusToolbarMinimumWidth = 360;
const wchar_t kToolbarClassName[] = L"QingYingSelectionToolbarV4";

ToolbarIconKind toModernToolbarIcon(SelectionToolbarIcon icon) noexcept;

const wchar_t* statusText(SelectionToolbarStatus status) noexcept {
  switch (status) {
    case SelectionToolbarStatus::Running:
      return L"\x81EA\x52A8\x957F\x622A\x56FE\x4E2D";
    case SelectionToolbarStatus::Paused:
      return L"\x957F\x622A\x56FE\x5DF2\x6682\x505C";
    case SelectionToolbarStatus::Finishing:
      return L"\x6B63\x5728\x7ED3\x675F\x957F\x622A\x56FE...";
    case SelectionToolbarStatus::Recoverable:
      return L"\x65E0\x6CD5\x7EE7\x7EED\xFF0C\x53EF\x91CD\x8BD5\x6216\x8C03\x6574\x9009\x533A";
    case SelectionToolbarStatus::SingleFramePending:
      return L"\x4EC5\x4FDD\x7559\x9996\x5E27\xFF0C\x53EF\x4F5C\x4E3A\x666E\x901A\x622A\x56FE";
    case SelectionToolbarStatus::PartialResultPending:
      return L"\x672A\x5B8C\x6574\xFF0C\x5DF2\x4FDD\x7559\x53EF\x9760\x7684\x957F\x56FE\x5185\x5BB9";
    case SelectionToolbarStatus::None:
      return L"";
  }
  return L"";
}

const wchar_t* commandTooltip(SelectionToolbarCommand command,
                              SelectionToolbarIcon icon) noexcept {
  switch (command) {
    case SelectionToolbarCommand::RetryLongShot:
      return L"\x91CD\x8BD5\x957F\x622A\x56FE";
    case SelectionToolbarCommand::AdjustLongShotSelection:
      return L"\x8C03\x6574\x9009\x533A";
    case SelectionToolbarCommand::KeepLongShotFrame:
      return L"\x4FDD\x7559\x666E\x901A\x622A\x56FE";
    case SelectionToolbarCommand::AcceptLongShotPartial:
      return L"\x63A5\x53D7\x5DF2\x6709\x5185\x5BB9";
    case SelectionToolbarCommand::CancelLongShot:
      return L"\x53D6\x6D88\x5E76\x653E\x5F03";
    default:
      return toolbarIconLabel(toModernToolbarIcon(icon));
  }
}

ToolbarIconKind toModernToolbarIcon(SelectionToolbarIcon icon) noexcept {
  switch (icon) {
    case SelectionToolbarIcon::Copy:
      return ToolbarIconKind::Copy;
    case SelectionToolbarIcon::Save:
      return ToolbarIconKind::Save;
    case SelectionToolbarIcon::LongShot:
      return ToolbarIconKind::LongShot;
    case SelectionToolbarIcon::Pause:
      return ToolbarIconKind::Pause;
    case SelectionToolbarIcon::Resume:
      return ToolbarIconKind::Resume;
    case SelectionToolbarIcon::Edit:
      return ToolbarIconKind::Edit;
    case SelectionToolbarIcon::Pin:
      return ToolbarIconKind::Pin;
    case SelectionToolbarIcon::Stop:
      return ToolbarIconKind::Stop;
  }
  return ToolbarIconKind::Copy;
}

}  // namespace

struct SelectionToolbar::Impl {
  struct Item {
    SelectionToolbarItemModel model;
    RECT rect{};
  };

  HWND hwnd{nullptr};
  HWND tooltip{nullptr};
  std::array<Item, SelectionToolbarItemCount> items{};
  std::array<std::array<wchar_t, kToolbarTooltipMaxChars>,
             SelectionToolbarItemCount>
      tooltip_text{};
  int hover{-1};
  int divider_x{0};
  OverlayPhase phase{OverlayPhase::Sniffing};
  LongShotRecoveryState recovery;
  SelectionToolbarPlacement placement;
  CommandCallback callback;

  int statusHeight() const noexcept {
    return selectionToolbarStatus(phase, recovery) ==
                   SelectionToolbarStatus::None
               ? 0
               : kStatusRowHeight;
  }

  void geometry(int& x, int& y, int& width, int& height) const noexcept {
    const ModernToolbarMetrics metrics = DefaultModernToolbarMetrics;
    width = modernToolbarWidth(static_cast<int>(SelectionToolbarItemCount),
                               metrics.divider_gap - metrics.gap, metrics);
    if (statusHeight() > 0) {
      width = (std::max)(width, kStatusToolbarMinimumWidth);
    }
    height = modernToolbarHeight(metrics) + statusHeight();
    const int selection_right =
        placement.selection_x + placement.selection_width;
    const int selection_bottom =
        placement.selection_y + placement.selection_height;
    if (selection_bottom + 8 + height <= placement.screen_bottom) {
      x = placement.selection_x;
      y = selection_bottom + 8;
    } else if (placement.selection_y - 8 - height >= placement.screen_top) {
      x = placement.selection_x;
      y = placement.selection_y - height - 8;
    } else if (selection_right + 8 + width <= placement.screen_right) {
      x = selection_right + 8;
      y = placement.selection_y;
    } else if (placement.selection_x - 8 - width >= placement.screen_left) {
      x = placement.selection_x - width - 8;
      y = placement.selection_y;
    } else {
      x = placement.selection_x;
      y = selection_bottom + 8;
    }
    x = (std::max)(placement.screen_left,
                   (std::min)(x, placement.screen_right - width));
    y = (std::max)(placement.screen_top,
                   (std::min)(y, placement.screen_bottom - height));
  }

  bool registerWindowClass() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = &Impl::windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor =
        LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kToolbarClassName;
    return RegisterClassExW(&wc) != 0 ||
           GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  }

  void applyWindowRegion(int width, int height) {
    if (hwnd == nullptr || width <= 0 || height <= 0) {
      return;
    }
    const HRGN region = CreateRoundRectRgn(
        0, 0, width + 1, height + 1,
        DefaultModernToolbarMetrics.corner_radius * 2,
        DefaultModernToolbarMetrics.corner_radius * 2);
    if (region != nullptr && SetWindowRgn(hwnd, region, TRUE) == 0) {
      DeleteObject(region);
    }
  }

  void layout(int height) {
    const ModernToolbarMetrics metrics = DefaultModernToolbarMetrics;
    const int y = statusHeight() +
                  (height - statusHeight() - metrics.item_size) / 2;
    int x = metrics.bar_padding;
    const SelectionToolbarItems model_items =
        buildSelectionToolbarItems(phase, recovery);
    for (std::size_t i = 0; i < items.size(); ++i) {
      items[i].model = model_items[i];
      items[i].rect = {x, y, x + metrics.item_size, y + metrics.item_size};
      x += metrics.item_size + metrics.gap;
      if (i == 1) {
        divider_x = x - metrics.gap + metrics.divider_gap / 2;
        x += metrics.divider_gap - metrics.gap;
      }
    }
  }

  int hitTest(int x, int y) const {
    const POINT point{x, y};
    for (std::size_t i = 0; i < items.size(); ++i) {
      if (PtInRect(&items[i].rect, point) != FALSE) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  void bindTooltips() {
    if (hwnd == nullptr) {
      return;
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
      const wchar_t* label = commandTooltip(items[i].model.command,
                                            items[i].model.icon);
      if (items[i].model.command == SelectionToolbarCommand::StopLongShot &&
          phase == OverlayPhase::LongShotFinishing) {
        label = L"\x505C\x6B62\x4E2D";
      }
      bindToolbarTooltip(tooltip, hwnd, static_cast<UINT>(i + 1),
                         items[i].rect, label, tooltip_text[i].data(),
                         static_cast<UINT>(tooltip_text[i].size()));
    }
  }

  void refresh(OverlayPhase next_phase,
               LongShotRecoveryState next_recovery = {}) {
    phase = next_phase;
    recovery = next_recovery;
    const SelectionToolbarItems model_items =
        buildSelectionToolbarItems(phase, recovery);
    for (std::size_t i = 0; i < items.size(); ++i) {
      items[i].model = model_items[i];
    }
    if (hwnd != nullptr) {
      int x = 0;
      int y = 0;
      int width = 0;
      int height = 0;
      geometry(x, y, width, height);
      SetWindowPos(hwnd, nullptr, x, y, width, height,
                   SWP_NOACTIVATE | SWP_NOZORDER);
      applyWindowRegion(width, height);
      layout(height);
      bindTooltips();
      InvalidateRect(hwnd, nullptr, FALSE);
    }
  }

  bool present() {
    if (hwnd == nullptr) {
      return false;
    }
    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    void* bits = nullptr;
    const HBITMAP dib = createTopDownArgbDib(width, height, &bits);
    const HDC mem_dc = dib != nullptr ? CreateCompatibleDC(nullptr) : nullptr;
    if (dib == nullptr || bits == nullptr || mem_dc == nullptr) {
      if (mem_dc != nullptr) {
        DeleteDC(mem_dc);
      }
      if (dib != nullptr) {
        DeleteObject(dib);
      }
      return false;
    }

    const HGDIOBJ old_bitmap = SelectObject(mem_dc, dib);
    std::memset(bits, 0, static_cast<std::size_t>(width) *
                             static_cast<std::size_t>(height) * 4u);
    if (!drawToolbarBarOnArgbBits(bits, width, height, client)) {
      // GDI+ 不可用时仍保留色键绘制作为离屏降级；提交窗口前再把色键
      // 转为透明 alpha，避免窗口 DC 暴露“先清空、后重画”的中间帧。
      fillToolbarColorKey(mem_dc, client);
      drawToolbarBar(mem_dc, client);
      applyColorKeyAlpha(bits, width, height, kToolbarColorKey);
    }

    const ModernToolbarMetrics metrics = DefaultModernToolbarMetrics;
    const int divider_pad = metrics.bar_padding + kToolbarDividerPadExtraPx;
    drawToolbarDivider(mem_dc, divider_x,
                       client.top + statusHeight() + divider_pad,
                       client.bottom - divider_pad);
    const SelectionToolbarStatus status =
        selectionToolbarStatus(phase, recovery);
    if (status != SelectionToolbarStatus::None) {
      RECT status_rect{DefaultModernToolbarMetrics.bar_padding, 4,
                       client.right - DefaultModernToolbarMetrics.bar_padding,
                       statusHeight()};
      const HGDIOBJ old_font =
          SelectObject(mem_dc, GetStockObject(DEFAULT_GUI_FONT));
      SetBkMode(mem_dc, TRANSPARENT);
      SetTextColor(mem_dc, DefaultModernToolbarColors.label);
      DrawTextW(mem_dc, statusText(status), -1, &status_rect,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
      SelectObject(mem_dc, old_font);
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
      const ToolbarItemModel toolbar_item{
          toModernToolbarIcon(items[i].model.icon),
          static_cast<int>(i) == hover,
          false,
          items[i].model.enabled,
          false,
          false};
      drawToolbarItem(mem_dc, items[i].rect, toolbar_item);
    }

    // hover 帧在内存中完整生成后一次性交给 DWM；不会再把透明清屏帧展示
    // 给用户，因此跨按钮和离开工具栏时都不会整条闪烁。
    promoteRgbToOpaqueAlpha(bits, width, height);
    const bool presented =
        presentLayeredArgbWindow(hwnd, mem_dc, width, height);

    SelectObject(mem_dc, old_bitmap);
    DeleteDC(mem_dc);
    DeleteObject(dib);
    return presented;
  }

  void paint() {
    if (hwnd == nullptr) {
      return;
    }
    PAINTSTRUCT ps{};
    const HDC hdc = BeginPaint(hwnd, &ps);
    if (hdc == nullptr) {
      return;
    }
    static_cast<void>(present());
    EndPaint(hwnd, &ps);
  }

  static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam,
                                     LPARAM lparam) {
    Impl* self = reinterpret_cast<Impl*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
      self = static_cast<Impl*>(create->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(self));
      if (self != nullptr) {
        self->hwnd = window;
      }
      return TRUE;
    }
    if (self == nullptr) {
      return DefWindowProcW(window, message, wparam, lparam);
    }

    switch (message) {
      case WM_CREATE: {
        RECT client{};
        GetClientRect(window, &client);
        self->layout(client.bottom - client.top);
        self->applyWindowRegion(client.right, client.bottom);
        self->tooltip = createToolbarTooltip(window);
        self->refresh(self->phase);
        return 0;
      }
      case WM_PAINT:
        self->paint();
        return 0;
      case WM_ERASEBKGND:
        return 1;
      case WM_MOUSEMOVE: {
        const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
        const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
        const int hit = self->hitTest(x, y);
        if (hit != self->hover) {
          self->hover = hit;
          InvalidateRect(window, nullptr, FALSE);
        }
        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = window;
        TrackMouseEvent(&track);
        return 0;
      }
      case WM_MOUSELEAVE:
        if (self->hover >= 0) {
          self->hover = -1;
          InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
      case WM_LBUTTONUP: {
        const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
        const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
        const int hit = self->hitTest(x, y);
        if (hit < 0 || static_cast<std::size_t>(hit) >= self->items.size() ||
            !self->items[static_cast<std::size_t>(hit)].model.enabled) {
          return 0;
        }
        const SelectionToolbarCommand command =
            self->items[static_cast<std::size_t>(hit)].model.command;
        const CommandCallback callback_copy = self->callback;
        if (callback_copy) {
          callback_copy(command);
        }
        return 0;
      }
      case WM_KEYDOWN:
        if (wparam == VK_ESCAPE && self->callback) {
          const CommandCallback callback_copy = self->callback;
          callback_copy(SelectionToolbarCommand::Cancel);
        }
        return 0;
      case WM_DESTROY:
        if (self->tooltip != nullptr) {
          DestroyWindow(self->tooltip);
          self->tooltip = nullptr;
        }
        self->hwnd = nullptr;
        self->hover = -1;
        self->callback = {};
        return 0;
      default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
  }
};

SelectionToolbar::SelectionToolbar() : impl_(std::make_unique<Impl>()) {}

SelectionToolbar::~SelectionToolbar() { hide(); }

bool SelectionToolbar::show(HWND owner_window,
                            const SelectionToolbarPlacement& placement,
                            OverlayPhase phase, CommandCallback callback,
                            LongShotRecoveryState recovery) {
  hide();
  if (!impl_->registerWindowClass()) {
    return false;
  }

  impl_->phase = phase;
  impl_->recovery = recovery;
  impl_->placement = placement;
  impl_->callback = std::move(callback);

  int x = 0;
  int y = 0;
  int toolbar_width = 0;
  int toolbar_height = 0;
  impl_->geometry(x, y, toolbar_width, toolbar_height);

  const HWND window = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
      kToolbarClassName, L"", WS_POPUP, x, y, toolbar_width, toolbar_height,
      owner_window,
      nullptr, GetModuleHandleW(nullptr), impl_.get());
  if (window == nullptr) {
    impl_->callback = {};
    return false;
  }

  // 分层窗口在首个 UpdateLayeredWindow 前完全透明。先同步提交首帧再显示，
  // 防止首个 WM_PAINT 被随后发生的整屏 Overlay 重绘或鼠标消息推迟。
  if (!impl_->present()) {
    DestroyWindow(window);
    return false;
  }
  static_cast<void>(ValidateRect(window, nullptr));
  if (SetWindowPos(window, HWND_TOPMOST, x, y, toolbar_width, toolbar_height,
                   SWP_SHOWWINDOW | SWP_NOACTIVATE) == FALSE) {
    DestroyWindow(window);
    return false;
  }
  return true;
}

void SelectionToolbar::update(OverlayPhase phase,
                              LongShotRecoveryState recovery) {
  impl_->refresh(phase, recovery);
}

void SelectionToolbar::hide() {
  if (impl_ == nullptr) {
    return;
  }
  if (impl_->hwnd != nullptr && IsWindow(impl_->hwnd)) {
    DestroyWindow(impl_->hwnd);
  } else {
    impl_->hwnd = nullptr;
    impl_->callback = {};
  }
}

bool SelectionToolbar::visible() const noexcept {
  return impl_ != nullptr && impl_->hwnd != nullptr;
}

}  // namespace qingying
