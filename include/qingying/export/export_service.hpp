#pragma once

#include "qingying/action/types.hpp"

#include <string>

namespace qingying {

class ExportService {
 public:
  ActionResult copyToClipboard();
  ActionResult savePng(const std::wstring& path);
};

}  // namespace qingying
