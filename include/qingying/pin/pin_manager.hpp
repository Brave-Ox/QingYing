#pragma once

#include "qingying/action/image.hpp"
#include "qingying/action/automation_limits.h"
#include "qingying/pin/pin_window.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace qingying {

class PinManager {
 public:
  using ImageActionCallback = PinWindow::ImageActionCallback;
  using CommitAuthorization = std::function<bool()>;
  using WindowPresenter = std::function<bool(PinWindow&, int, int)>;

  struct CreationResult {
    int error_code{ErrorCode::kUnknown};
    PinId pin_id{kInvalidPinId};
    explicit operator bool() const noexcept {
      return error_code == ErrorCode::kOk && pin_id != kInvalidPinId;
    }
  };

  struct AgentUsage {
    std::uint32_t count{0};
    std::uint64_t bytes{0};
  };

  class CaptureGuard {
   public:
    explicit CaptureGuard(PinManager& manager);
    ~CaptureGuard();

    CaptureGuard(const CaptureGuard&) = delete;
    CaptureGuard& operator=(const CaptureGuard&) = delete;

   private:
    PinManager* manager_;
  };

  explicit PinManager(AutomationLimits limits = {},
                      WindowPresenter presenter = {});
  ~PinManager();

  // Compatibility adapter for existing GUI callers.
  bool show(const Image& image);
  CreationResult showGui(const Image& image);
  CreationResult showAgent(const Image& image,
                           CommitAuthorization authorize_commit);
  void closeAll();
  int count() const;
  AgentUsage agentUsage() const noexcept { return agent_usage_; }
  bool contains(PinId pin_id) const noexcept;
  std::optional<PinSource> source(PinId pin_id) const noexcept;
  void setActionCallbacks(ImageActionCallback copy_callback,
                          ImageActionCallback save_callback);
  CaptureGuard temporarilyHideForCapture();
  bool captureExclusionActive() const noexcept {
    return capture_exclusion_depth_ > 0;
  }

  // 返回所有钉图窗口的屏幕矩形（测试 / 诊断辅助）。
  std::vector<RECT> windowRects() const;

 private:
  // 为新钉图（宽 width 高 height）计算不与任何已有钉图重叠的初始位置：
  // 从虚拟桌面右侧开始、从右往左逐列依次排列，排满换行，并避让已有窗口。
  void computeNextPinPosition(int width, int height, int& out_x,
                              int& out_y) const;
  void beginCaptureExclusion();
  void endCaptureExclusion();
  CreationResult create(const Image& image, PinSource source,
                        CommitAuthorization authorize_commit);
  PinId allocatePinId() noexcept;
  void releaseAgentBudget(std::uint64_t bytes) noexcept;
  void onWindowClosed(PinWindow* window);

  struct Entry {
    std::unique_ptr<PinWindow> window;
    std::uint64_t agent_bytes{0};
    bool agent_accounted{false};
  };
  std::vector<Entry> windows_;
  ImageActionCallback copy_callback_;
  ImageActionCallback save_callback_;
  std::vector<HWND> hidden_windows_;
  int capture_exclusion_depth_{0};
  AutomationLimits limits_;
  WindowPresenter presenter_;
  AgentUsage agent_usage_;
  PinId next_pin_id_{1};
};

}  // namespace qingying
