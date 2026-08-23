#include "qingying/export/export_service.hpp"

namespace qingying {

ActionResult ExportService::copyToClipboard() {
  ActionResult r;
  r.ok = false;
  r.error_code = ErrorCode::kNotImplemented;
  r.message = "ExportService::copyToClipboard stub";
  return r;
}

ActionResult ExportService::savePng(const std::wstring& /*path*/) {
  ActionResult r;
  r.ok = false;
  r.error_code = ErrorCode::kNotImplemented;
  r.message = "ExportService::savePng stub";
  return r;
}

}  // namespace qingying
