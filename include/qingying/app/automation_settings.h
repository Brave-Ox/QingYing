#pragma once
#include <string>
#include <vector>
namespace qingying {
class AutomationSettings final {
 public:
  explicit AutomationSettings(std::wstring test_namespace = {});
  bool enabled() const;
  bool setEnabled(bool enabled) const;
  std::vector<std::wstring> allowedSaveDirectories() const;
  const std::wstring& key() const { return key_; }
 private:
  std::wstring key_;
  bool testing_{false};
};
}  // namespace qingying
