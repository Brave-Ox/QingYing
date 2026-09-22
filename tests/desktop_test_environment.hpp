#pragma once

#include "qingying/capture/capture_engine.hpp"

#include <Windows.h>

#include <string>

namespace qingying::test {

struct DesktopCaptureProbe {
  bool available{false};
  std::string diagnostic;
};

// Real desktop capture tests must not turn a non-interactive CI worker into a
// product failure. The probe uses the same minimal 1x1 capture path as the
// desktop scenarios and returns its diagnostic for GTEST_SKIP.
inline DesktopCaptureProbe probeDesktopCapture() {
  const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if (width <= 0 || height <= 0) {
    return {false, "virtual desktop is unavailable"};
  }

  CaptureEngine engine;
  Image image;
  const ActionResult result = engine.captureRegion(x, y, 1, 1, image);
  if (!result.ok || image.empty()) {
    return {false, result.message.empty() ? "desktop capture is unavailable"
                                          : result.message};
  }
  return {true, {}};
}

}  // namespace qingying::test
