#pragma once

#include "qingying/action/types.hpp"

#include <string>
#include <vector>

namespace qingying {

struct ValidatedSavePath {
  std::wstring absolute_path;
  bool overwrite{false};
};

// Application-layer authorization for non-interactive saves. The policy only
// accepts an existing local directory below an explicitly configured root and
// a single PNG filename. Filesystem identity is checked in addition to lexical
// containment; reparse points are rejected throughout the allowed path.
class SavePolicy final {
 public:
  explicit SavePolicy(std::vector<std::wstring> allowed_directories);

  ActionResult validate(const std::wstring& directory,
                        const std::wstring& name, bool overwrite,
                        ValidatedSavePath* output) const;
  ActionResult validateFullPath(const std::wstring& path, bool overwrite,
                                ValidatedSavePath* output) const;

 private:
  std::vector<std::wstring> allowed_directories_;
};

}  // namespace qingying
