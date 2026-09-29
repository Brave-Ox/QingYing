#pragma once

#include "qingying/window/window_detector.hpp"

#include <Windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace qingying::window_detail {

using BoundsReader = std::function<bool(HWND, RECT&)>;

bool ownProcess(HWND hwnd) noexcept;
bool desktopShell(HWND hwnd) noexcept;
bool cloaked(HWND hwnd) noexcept;
bool isTaskbarWindowClass(std::wstring_view class_name) noexcept;
bool isTaskbarWindow(HWND hwnd) noexcept;
bool commonCandidate(HWND hwnd) noexcept;
bool isBrowserOwnedTransientPopupStyle(LONG_PTR window_style,
                                       bool has_browser_owner) noexcept;
bool isBrowserOwnedTransientPopup(HWND hwnd) noexcept;
bool readVisibleBounds(HWND hwnd, RECT& bounds,
                       BoundsReader dwm_reader = {},
                       BoundsReader fallback_reader = {});
bool intersectsVirtualDesktop(const RECT& bounds) noexcept;
bool fullyInsideVirtualDesktop(const RECT& bounds) noexcept;
std::uint32_t processId(HWND hwnd) noexcept;
std::wstring windowTitle(HWND hwnd);
bool invariantTitleMatch(const std::wstring& title,
                         const std::wstring& query,
                         bool exact) noexcept;

}  // namespace qingying::window_detail
