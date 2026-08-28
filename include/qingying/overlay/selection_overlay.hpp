#pragma once

#include "qingying/action/image.hpp"

#include <atomic>
#include <cstdint>
#include <functional>

namespace qingying {

enum class SelectionAction {
  None,
  Copy,
  Save,
  Edit,
  Pin,
  LongShot,
};

enum class LongShotControl {
  TogglePause,
  Stop,
};

struct SelectionResult {
  bool cancelled{true};
  int x{0};
  int y{0};
  int width{0};
  int height{0};
  SelectionAction action{SelectionAction::None};
};

using SelectionCallback = std::function<void(const SelectionResult&)>;
using LongShotControlCallback = std::function<void(LongShotControl)>;

class SelectionOverlay {
 public:
  // Show fullscreen mask; after selection, display an action toolbar near the
  // region and invoke the callback when the user chooses an action or cancels.
  // `background` is the desktop snapshot (physical pixels) shown inside the
  // overlay so other windows (incl. owned popups) stay visible in the mask UI;
  // an empty image falls back to a pure translucent mask.
  // Returns false if overlay could not be shown.
  bool show(const Image& background, SelectionCallback callback,
            LongShotControlCallback longshot_control_callback = {});

  // These methods are safe to call from the long-shot worker thread. Updates
  // are posted back to the overlay's UI thread.
  bool postLongShotPreview(const Image& image);
  bool postLongShotFinished(bool success);

  void hide();

 private:
  std::atomic<std::uintptr_t> overlay_hwnd_{0};
};

}  // namespace qingying
