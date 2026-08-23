#pragma once

#include "qingying/action/types.hpp"

namespace qingying {

// PIMPL: DXGI/GDI stay in .cpp — other modules only see this header.
class CaptureEngine {
 public:
  CaptureEngine();
  ~CaptureEngine();

  CaptureEngine(const CaptureEngine&) = delete;
  CaptureEngine& operator=(const CaptureEngine&) = delete;

  ActionResult captureRegion(int x, int y, int width, int height);
  ActionResult captureWindow(const std::wstring& query);
  ActionResult cropCenter(int width, int height);

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace qingying
