#pragma once

#include "qingying/action/image.hpp"
#include "qingying/action/action_result.hpp"

#include <string>
#include <functional>

namespace qingying {

class ExportService {
 public:
  struct PngSaveOptions {
    bool overwrite{false};
    // Called after encoding and immediately before the filesystem commit.
    // An empty callback grants commit permission for trusted GUI callers.
    std::function<bool()> authorize_commit;
  };

  ActionResult copyToClipboard(const Image& image);
  // Compatibility path for the GUI, whose save dialog already confirms
  // replacement. Non-interactive callers must pass explicit options.
  ActionResult savePng(const Image& image, const std::wstring& path);
  ActionResult savePng(const Image& image, const std::wstring& path,
                       PngSaveOptions options);
};

}  // namespace qingying
