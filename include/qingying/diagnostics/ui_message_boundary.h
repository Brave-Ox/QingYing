#pragma once
#include "qingying/diagnostics/fault_boundary.h"
#include <Windows.h>

namespace qingying {
// Called only from a catch at a Win32 callback. Keep lifecycle/default handling
// available after rejecting the failed message and retire a failed paint.
inline LRESULT recoverUiMessage(HWND window, UINT message, WPARAM wparam,
                                LPARAM lparam,
                                const char* provider = nullptr) noexcept {
  recordFault(ErrorCode::kUnknown, FaultOrigin::Ui, FaultDomain::Request,
              provider);
  if (message == WM_NCCREATE) return FALSE;
  if (message == WM_CREATE) return -1;
  if (message == WM_PAINT) { ValidateRect(window, nullptr); return 0; }
  if (message == WM_NCDESTROY) SetWindowLongPtrW(window, GWLP_USERDATA, 0);
  return DefWindowProcW(window, message, wparam, lparam);
}
}  // namespace qingying
