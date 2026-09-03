#include "qingying/overlay/selection_toolbar.hpp"

#include <Windows.h>

#include <algorithm>
#include <utility>

#include "qingying/ui/modern_toolbar.hpp"

namespace qingying {

SelectionToolbarItems buildSelectionToolbarItems(OverlayPhase phase) noexcept {
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

namespace {

constexpr int kToolbarDividerPadExtraPx = 6;
const wchar_t kToolbarClassName[] = L"QingYingSelectionToolbarV4";

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
  CommandCallback callback;

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

  void layout(int height) {
    const ModernToolbarMetrics metrics = DefaultModernToolbarMetrics;
    const int y = (height - metrics.item_size) / 2;
    int x = metrics.bar_padding;
    const SelectionToolbarItems model_items =
        buildSelectionToolbarItems(phase);
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
      const ToolbarIconKind icon = toModernToolbarIcon(items[i].model.icon);
      const wchar_t* label = toolbarIconLabel(icon);
      if (items[i].model.command == SelectionToolbarCommand::StopLongShot &&
          phase == OverlayPhase::LongShotFinishing) {
        label = L"\x505C\x6B62\x4E2D";
      }
      bindToolbarTooltip(tooltip, hwnd, static_cast<UINT>(i + 1),
                         items[i].rect, label, tooltip_text[i].data(),
                         static_cast<UINT>(tooltip_text[i].size()));
    }
  }

  void refresh(OverlayPhase next_phase) {
    phase = next_phase;
    const SelectionToolbarItems model_items =
        buildSelectionToolbarItems(phase);
    for (std::size_t i = 0; i < items.size(); ++i) {
      items[i].model = model_items[i];
    }
    if (hwnd != nullptr) {
      bindTooltips();
      InvalidateRect(hwnd, nullptr, FALSE);
    }
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
    RECT client{};
    GetClientRect(hwnd, &client);
    fillToolbarColorKey(hdc, client);
    drawToolbarBar(hdc, client);

    const ModernToolbarMetrics metrics = DefaultModernToolbarMetrics;
    const int divider_pad = metrics.bar_padding + kToolbarDividerPadExtraPx;
    drawToolbarDivider(hdc, divider_x, client.top + divider_pad,
                       client.bottom - divider_pad);
    for (std::size_t i = 0; i < items.size(); ++i) {
      drawToolbarItem(hdc, items[i].rect,
                      toModernToolbarIcon(items[i].model.icon),
                      static_cast<int>(i) == hover, false,
                      items[i].model.enabled, false);
    }
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
        applyToolbarColorKey(window);
        const HRGN region = CreateRoundRectRgn(
            0, 0, client.right + 1, client.bottom + 1,
            DefaultModernToolbarMetrics.corner_radius * 2,
            DefaultModernToolbarMetrics.corner_radius * 2);
        if (region != nullptr) {
          if (SetWindowRgn(window, region, TRUE) == 0) {
            DeleteObject(region);
          }
        }
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
                            OverlayPhase phase, CommandCallback callback) {
  hide();
  if (!impl_->registerWindowClass()) {
    return false;
  }

  impl_->phase = phase;
  impl_->callback = std::move(callback);

  const ModernToolbarMetrics metrics = DefaultModernToolbarMetrics;
  const int toolbar_width = modernToolbarWidth(
      static_cast<int>(SelectionToolbarItemCount),
      metrics.divider_gap - metrics.gap, metrics);
  const int toolbar_height = modernToolbarHeight(metrics);

  int x = placement.selection_x;
  int y = placement.selection_y + placement.selection_height + 8;
  if (y + toolbar_height > placement.screen_bottom) {
    y = placement.selection_y - toolbar_height - 8;
  }
  x = (std::max)(placement.screen_left,
                 (std::min)(x, placement.screen_right - toolbar_width));
  y = (std::max)(placement.screen_top,
                 (std::min)(y, placement.screen_bottom - toolbar_height));

  const HWND window = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kToolbarClassName,
      L"", WS_POPUP | WS_VISIBLE, x, y, toolbar_width, toolbar_height,
      owner_window,
      nullptr, GetModuleHandleW(nullptr), impl_.get());
  if (window == nullptr) {
    impl_->callback = {};
    return false;
  }
  SetWindowPos(window, HWND_TOPMOST, x, y, toolbar_width, toolbar_height,
               SWP_SHOWWINDOW | SWP_NOACTIVATE);
  return true;
}

void SelectionToolbar::update(OverlayPhase phase) { impl_->refresh(phase); }

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
