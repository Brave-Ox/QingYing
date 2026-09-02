#include "qingying/longshot/notepad_longshot_profile.hpp"

#include "notepad_longshot_resolver.h"
#include "win32_scroll_helpers.hpp"

namespace qingying {

bool resolveNotepadProfile(std::uintptr_t owner_window,
                           LongShotProfileResult& out) {
  return longshot_detail::resolveNotepadTarget(owner_window, out);
}

bool NotepadLongShotProfile::resolve(const LongShotRequest& request,
                                     LongShotProfileResult& out) const {
  if (!resolveNotepadProfile(request.owner_window, out)) {
    return false;
  }
  return out.containsSelection(request.x, request.y, request.width,
                               request.height);
}

bool NotepadLongShotProfile::scrollDown(
    const LongShotRequest& request,
    const LongShotProfileResult& profile) const {
  return longshot_detail::sendWheelDown(request, profile);
}

bool NotepadLongShotProfile::queryScrollState(
    const LongShotProfileResult& profile, LongShotScrollState& out) const {
  return longshot_detail::queryVerticalScrollState(profile, out);
}

bool queryNotepadScrollAtBottom(const LongShotProfileResult& profile,
                                bool& at_bottom) {
  LongShotScrollState state;
  if (!longshot_detail::queryVerticalScrollState(profile, state)) {
    at_bottom = false;
    return false;
  }
  at_bottom = state.atBottom();
  return true;
}

}  // namespace qingying
