#pragma once

#include "qingying/action/image.hpp"
#include "qingying/action/types.hpp"
#include "qingying/export/export_service.hpp"

#include <string>

namespace qingying::detail {

ActionResult savePngFileTransaction(
    const Image& image, const std::wstring& path,
    ExportService::PngSaveOptions options);

}  // namespace qingying::detail
