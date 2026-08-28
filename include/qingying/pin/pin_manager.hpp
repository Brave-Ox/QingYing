#pragma once

#include "qingying/action/image.hpp"
#include "qingying/pin/pin_window.hpp"

#include <memory>
#include <vector>

namespace qingying {

class PinManager {
 public:
  using ImageActionCallback = PinWindow::ImageActionCallback;

  class CaptureGuard {
   public:
    explicit CaptureGuard(PinManager& manager);
    ~CaptureGuard();

    CaptureGuard(const CaptureGuard&) = delete;
    CaptureGuard& operator=(const CaptureGuard&) = delete;

   private:
    PinManager* manager_;
  };

  ~PinManager();

  bool show(const Image& image);
  void closeAll();
  int count() const;
  void setActionCallbacks(ImageActionCallback copy_callback,
                          ImageActionCallback save_callback);
  CaptureGuard temporarilyHideForCapture();

  // 返回所有钉图窗口的屏幕矩形（测试 / 诊断辅助）。
  std::vector<RECT> windowRects() const;

 private:
  // 为新钉图（宽 width 高 height）计算不与任何已有钉图重叠的初始位置：
  // 从虚拟桌面右侧开始、从右往左逐列依次排列，排满换行，并避让已有窗口。
  void computeNextPinPosition(int width, int height, int& out_x,
                              int& out_y) const;
  void beginCaptureExclusion();
  void endCaptureExclusion();
  void onWindowClosed(PinWindow* window);

  std::vector<std::unique_ptr<PinWindow>> windows_;
  ImageActionCallback copy_callback_;
  ImageActionCallback save_callback_;
  std::vector<HWND> hidden_windows_;
  int capture_exclusion_depth_{0};
};

}  // namespace qingying
