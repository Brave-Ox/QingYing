#include "qingying/overlay/coordinate_transform.hpp"

#include <Windows.h>

namespace qingying {
namespace coord {

VirtualScreenRect getVirtualScreen() {
  VirtualScreenRect r;
  r.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
  r.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
  r.width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  r.height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  return r;
}

int getSystemDpi() {
  HDC dc = GetDC(nullptr);
  if (dc == nullptr) {
    return 96;
  }
  const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
  ReleaseDC(nullptr, dc);
  return dpi > 0 ? dpi : 96;
}

}  // namespace coord
}  // namespace qingying
