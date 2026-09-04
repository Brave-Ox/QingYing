#pragma once

#include <Windows.h>

#include <cstdint>
#include <vector>

#include "qingying/action/image.hpp"
#include "qingying/geometry/rect_types.h"
#include "qingying/overlay/coordinate_transform.hpp"
#include "qingying/overlay/overlay_phase.hpp"

namespace qingying {

// Immutable view of the overlay state needed to compose one frame. The
// SelectionOverlay owns the state and remains responsible for input, phase
// transitions, and callbacks; OverlayRenderer only turns this view into
// pixels and presents them through UpdateLayeredWindow.
struct OverlayRenderState {
  const OverlayClientRect& selection;
  const OverlayClientRect& hover_rect;
  const Image& background;
  const Image& longshot_preview;
  OverlayPhase phase{OverlayPhase::Sniffing};
  bool show_handles{false};
  int handle_radius{0};
  bool show_hover{false};
  bool capture_passthrough{false};

  OverlayRenderState(const OverlayClientRect& selection_in,
                     const OverlayClientRect& hover_rect_in,
                     const Image& background_in,
                     const Image& longshot_preview_in, OverlayPhase phase_in,
                     bool show_handles_in, int handle_radius_in,
                     bool show_hover_in, bool capture_passthrough_in)
      : selection(selection_in),
        hover_rect(hover_rect_in),
        background(background_in),
        longshot_preview(longshot_preview_in),
        phase(phase_in),
        show_handles(show_handles_in),
        handle_radius(handle_radius_in),
        show_hover(show_hover_in),
        capture_passthrough(capture_passthrough_in) {}
};

class OverlayRenderer {
 public:
  // Compose the overlay frame and present it in a layered window.
  static bool render(HWND hwnd, const coord::VirtualScreenRect& screen,
                     const OverlayRenderState& state);

  // Pure pixel composition used by render() and by off-screen tests. On
  // failure, out_pixels is left unchanged.
  static bool renderPixels(int width, int height,
                           const OverlayRenderState& state,
                           std::vector<std::uint32_t>& out_pixels);

  // Build the bounded nearest-neighbour preview shown during long-shot
  // capture. Empty input returns an empty image.
  static Image makeLongShotPreviewImage(const Image& source);
};

}  // namespace qingying
