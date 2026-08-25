#pragma once

#include "qingying/action/image.hpp"
#include "qingying/pin/pin_window.hpp"

#include <memory>
#include <vector>

namespace qingying {

class PinManager {
 public:
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
  CaptureGuard temporarilyHideForCapture();

 private:
  void beginCaptureExclusion();
  void endCaptureExclusion();
  void onWindowClosed(PinWindow* window);

  std::vector<std::unique_ptr<PinWindow>> windows_;
  std::vector<HWND> hidden_windows_;
  int capture_exclusion_depth_{0};
};

}  // namespace qingying
