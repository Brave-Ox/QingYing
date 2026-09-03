#pragma once

#include "qingying/action/image.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

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
  // 标注确认后的合成图可随结果工具栏继续传递；普通选区保持 empty。
  Image annotated_image;
};

using SelectionCallback = std::function<void(const SelectionResult&)>;
using LongShotControlCallback = std::function<void(LongShotControl)>;
using SelectionClosedCallback = std::function<void()>;

class SelectionOverlay {
 public:
  SelectionOverlay();
  ~SelectionOverlay();

  SelectionOverlay(const SelectionOverlay&) = delete;
  SelectionOverlay& operator=(const SelectionOverlay&) = delete;

  // Show fullscreen mask; after selection, display an action toolbar near the
  // region and invoke the callback when the user chooses an action or cancels.
  // The window is non-modal: this method returns after creation and the
  // application's normal message loop drives all subsequent interaction.
  // `background` is the desktop snapshot (physical pixels) shown inside the
  // overlay so other windows (incl. owned popups) stay visible in the mask UI;
  // an empty image falls back to a pure translucent mask.
  // `initial_selection` 可用于标注完成后恢复同一选区及结果操作条；其坐标
  // 使用物理屏幕坐标，与最终回调一致。
  // `closed_callback` 在窗口销毁后调用，可用于推进外层 Workflow；中止
  // / 应用退出时回调会被清空，不会交付半成品选区。
  // Returns false if overlay could not be shown.
  bool show(const Image& background, SelectionCallback callback,
            LongShotControlCallback longshot_control_callback = {},
            const SelectionResult& initial_selection = {},
            SelectionClosedCallback closed_callback = {});

  // These methods are safe to call from the long-shot worker thread. Updates
  // are posted back to the overlay's UI thread.
  bool postLongShotPreview(const Image& image);
  bool postLongShotFinished(bool success);

  bool isVisible() const noexcept;

  // Silently abort and destroy the current window. Selection callbacks are
  // suppressed, which is used by workflow cancellation and application exit.
  void hide();

  // Drops queued worker events after the producer has stopped. The window
  // procedure also drains during destruction as a final safety net.
  void drainMessages() noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::atomic<std::uintptr_t> overlay_hwnd_{0};
};

}  // namespace qingying
