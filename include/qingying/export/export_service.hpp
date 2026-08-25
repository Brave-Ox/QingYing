#pragma once

#include "qingying/action/image.hpp"
#include "qingying/action/types.hpp"

#include <string>

namespace qingying {

class ExportService {
 public:
  ActionResult copyToClipboard(const Image& image);
  ActionResult savePng(const Image& image, const std::wstring& path);
};

}  // namespace qingying
