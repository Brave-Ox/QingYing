#include "qingying/longshot/notepad_longshot_profile.hpp"

#include <Windows.h>

#include <cstdint>
#include <cwchar>
#include <vector>

namespace qingying {

namespace {

constexpr int kWindowClassCapacity = 128;
constexpr DWORD kProcessPathCapacity = 32768;

bool classNameEquals(HWND window, const wchar_t* expected) {
  wchar_t class_name[kWindowClassCapacity] = {};
  const int length =
      GetClassNameW(window, class_name, kWindowClassCapacity);
  return length > 0 && _wcsicmp(class_name, expected) == 0;
}

bool executableIsNotepad(HWND window) {
  DWORD process_id = 0;
  GetWindowThreadProcessId(window, &process_id);
  if (process_id == 0) {
    return false;
  }

  const HANDLE process =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
  if (process == nullptr) {
    return false;
  }

  std::vector<wchar_t> path(kProcessPathCapacity, L'\0');
  DWORD path_size = static_cast<DWORD>(path.size());
  const BOOL query_ok =
      QueryFullProcessImageNameW(process, 0, path.data(), &path_size);
  CloseHandle(process);
  if (!query_ok || path_size == 0) {
    return false;
  }

  const wchar_t* base_name = path.data();
  for (DWORD i = 0; i < path_size; ++i) {
    if (path[i] == L'\\' || path[i] == L'/') {
      base_name = path.data() + i + 1;
    }
  }
  return _wcsicmp(base_name, L"notepad.exe") == 0;
}

struct NotepadIdentityContext {
  bool found{false};
};

BOOL CALLBACK findNotepadProcess(HWND window, LPARAM parameter) {
  auto* context = reinterpret_cast<NotepadIdentityContext*>(parameter);
  if (executableIsNotepad(window)) {
    context->found = true;
    return FALSE;
  }
  return TRUE;
}

bool ownerBelongsToNotepad(HWND owner) {
  // Classic and current Win32 Notepad use this top-level class. The process
  // fallback also covers versions hosted below a system frame window.
  if (classNameEquals(owner, L"Notepad") || executableIsNotepad(owner)) {
    return true;
  }

  NotepadIdentityContext context;
  EnumChildWindows(owner, findNotepadProcess,
                   reinterpret_cast<LPARAM>(&context));
  return context.found;
}

bool isEditorWindow(HWND window) {
  wchar_t class_name[kWindowClassCapacity] = {};
  const int length =
      GetClassNameW(window, class_name, kWindowClassCapacity);
  if (length <= 0) {
    return false;
  }

  if (_wcsicmp(class_name, L"Edit") == 0) {
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    return (style & ES_MULTILINE) != 0;
  }

  constexpr wchar_t kRichEditPrefix[] = L"RichEdit";
  constexpr std::size_t kRichEditPrefixLength =
      (sizeof(kRichEditPrefix) / sizeof(kRichEditPrefix[0])) - 1;
  return static_cast<std::size_t>(length) >= kRichEditPrefixLength &&
         _wcsnicmp(class_name, kRichEditPrefix, kRichEditPrefixLength) == 0;
}

bool getClientScreenRect(HWND window, RECT& out) {
  RECT client{};
  if (!GetClientRect(window, &client)) {
    return false;
  }

  POINT top_left{client.left, client.top};
  POINT bottom_right{client.right, client.bottom};
  if (!ClientToScreen(window, &top_left) ||
      !ClientToScreen(window, &bottom_right)) {
    return false;
  }
  if (bottom_right.x <= top_left.x || bottom_right.y <= top_left.y) {
    return false;
  }

  out = {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
  return true;
}

struct EditorCandidate {
  HWND window{nullptr};
  RECT screen_rect{};
  std::int64_t area{0};
};

BOOL CALLBACK findLargestEditor(HWND window, LPARAM parameter) {
  auto* candidate = reinterpret_cast<EditorCandidate*>(parameter);
  if (!IsWindowVisible(window) || !isEditorWindow(window)) {
    return TRUE;
  }

  RECT screen_rect{};
  if (!getClientScreenRect(window, screen_rect)) {
    return TRUE;
  }

  const std::int64_t width =
      static_cast<std::int64_t>(screen_rect.right) - screen_rect.left;
  const std::int64_t height =
      static_cast<std::int64_t>(screen_rect.bottom) - screen_rect.top;
  const std::int64_t area = width * height;
  if (area > candidate->area) {
    candidate->window = window;
    candidate->screen_rect = screen_rect;
    candidate->area = area;
  }
  return TRUE;
}

}  // namespace

bool LongShotProfileResult::valid() const {
  return scroll_target != 0 && content_width > 0 && content_height > 0;
}

bool LongShotProfileResult::containsSelection(int x, int y, int width,
                                              int height) const {
  if (!valid() || width <= 0 || height <= 0) {
    return false;
  }

  const std::int64_t selection_left = x;
  const std::int64_t selection_top = y;
  const std::int64_t selection_right =
      selection_left + static_cast<std::int64_t>(width);
  const std::int64_t selection_bottom =
      selection_top + static_cast<std::int64_t>(height);
  const std::int64_t content_left = content_x;
  const std::int64_t content_top = content_y;
  const std::int64_t content_right =
      content_left + static_cast<std::int64_t>(content_width);
  const std::int64_t content_bottom =
      content_top + static_cast<std::int64_t>(content_height);

  return selection_left >= content_left && selection_top >= content_top &&
         selection_right <= content_right &&
         selection_bottom <= content_bottom;
}

bool resolveNotepadProfile(std::uintptr_t owner_window,
                           LongShotProfileResult& out) {
  out = LongShotProfileResult{};
  const HWND owner = reinterpret_cast<HWND>(owner_window);
  if (owner == nullptr || !IsWindow(owner) || !IsWindowVisible(owner) ||
      IsIconic(owner)) {
    return false;
  }
  if (GetAncestor(owner, GA_ROOT) != owner || !ownerBelongsToNotepad(owner)) {
    return false;
  }

  EditorCandidate candidate;
  EnumChildWindows(owner, findLargestEditor,
                   reinterpret_cast<LPARAM>(&candidate));
  if (candidate.window == nullptr) {
    return false;
  }

  out.scroll_target = reinterpret_cast<std::uintptr_t>(candidate.window);
  out.content_x = static_cast<int>(candidate.screen_rect.left);
  out.content_y = static_cast<int>(candidate.screen_rect.top);
  out.content_width =
      static_cast<int>(candidate.screen_rect.right - candidate.screen_rect.left);
  out.content_height = static_cast<int>(candidate.screen_rect.bottom -
                                        candidate.screen_rect.top);
  if (!out.valid()) {
    out = LongShotProfileResult{};
    return false;
  }
  return true;
}

bool queryNotepadScrollAtBottom(const LongShotProfileResult& profile,
                                bool& at_bottom) {
  at_bottom = false;
  if (!profile.valid()) {
    return false;
  }

  const HWND scroll_target = reinterpret_cast<HWND>(profile.scroll_target);
  if (scroll_target == nullptr || !IsWindow(scroll_target)) {
    return false;
  }

  SCROLLINFO scroll_info{};
  scroll_info.cbSize = sizeof(scroll_info);
  scroll_info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  if (!GetScrollInfo(scroll_target, SB_VERT, &scroll_info) ||
      scroll_info.nPage == 0) {
    return false;
  }

  // nMax is inclusive. With a page size, the last legal position is
  // nMax - nPage + 1. Use 64-bit arithmetic for defensive overflow safety.
  const std::int64_t last_position =
      static_cast<std::int64_t>(scroll_info.nMax) -
      static_cast<std::int64_t>(scroll_info.nPage) + 1;
  at_bottom = static_cast<std::int64_t>(scroll_info.nPos) >= last_position;
  return true;
}

}  // namespace qingying
