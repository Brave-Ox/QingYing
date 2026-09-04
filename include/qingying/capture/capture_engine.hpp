#pragma once

#include "qingying/action/image.hpp"
#include "qingying/action/types.hpp"
#include "qingying/geometry/rect_types.h"

#include <memory>

namespace qingying {

// PIMPL: DXGI/GDI stay in .cpp — other modules only see this header.
class CaptureEngine {
 public:
  CaptureEngine();
  ~CaptureEngine();

  CaptureEngine(const CaptureEngine&) = delete;
  CaptureEngine& operator=(const CaptureEngine&) = delete;

  // On success, `out` receives the BGRA32 frame (physical pixels).
  ActionResult captureRegion(const ScreenPhysicalRect& region, Image& out);
  ActionResult captureRegion(int x, int y, int width, int height, Image& out);
  ActionResult captureWindow(const std::wstring& query, Image& out);
  ActionResult cropCenter(int width, int height, Image& out);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace qingying
