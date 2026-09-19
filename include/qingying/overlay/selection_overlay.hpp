#pragma once

#include "qingying/action/image.hpp"
#include "qingying/overlay/selection_types.h"
#include "qingying/ui/shortcut_types.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <cstdint>
#include <functional>
#include <memory>

namespace qingying {

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
  bool show(Image background, SelectionCallback callback,
            LongShotControlCallback longshot_control_callback = {},
            const SelectionIntent& initial_selection = {},
            SelectionClosedCallback closed_callback = {},
            bool initial_selection_locked = false);

  // Each visible overlay consumes an immutable shortcut snapshot. Subsequent
  // settings changes take effect only when the next screenshot starts.
  bool show(Image background, SelectionCallback callback,
            LongShotControlCallback longshot_control_callback,
            const SelectionIntent& initial_selection,
            SelectionClosedCallback closed_callback,
            bool initial_selection_locked,
            const SelectionShortcutSettings& selection_shortcuts);

  // These methods are safe to call from the long-shot worker thread. Updates
  // are posted back to the overlay's UI thread.
  bool postLongShotPreview(const Image& image);
  bool postLongShotPreview(const Image& image, std::uint64_t session_id);
  // 设置新会话时释放旧预览载荷；已排队的旧 token 不再显示。
  void setLongShotSession(std::uint64_t session_id) noexcept;
  bool postLongShotFinished(bool success);

  bool isVisible() const noexcept;

  // Silently abort and destroy the current window. Selection callbacks are
  // suppressed, which is used by workflow cancellation and application exit.
  void hide();

  // Permanently stops the accessibility query worker, then closes the window
  // and drains worker messages. Call this during application shutdown before
  // the overlay's dependent services are reclaimed.
  void beginShutdown() noexcept;
  bool joinUntil(std::chrono::steady_clock::time_point deadline) noexcept;
  std::string diagnosticSnapshot() const;
  void finishShutdown() noexcept;
  void shutdown();

  // Drops queued worker events after the producer has stopped. The window
  // procedure also drains during destruction as a final safety net.
  void drainMessages() noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::atomic<std::uintptr_t> overlay_hwnd_{0};
};

}  // namespace qingying
