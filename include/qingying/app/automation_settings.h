#pragma once
#include <string>
namespace qingying {
class AutomationSettings final {
 public:
  explicit AutomationSettings(std::wstring test_namespace = {});
  bool enabled() const;
  bool setEnabled(bool enabled) const;
  const std::wstring& key() const { return key_; }
 private:
  std::wstring key_;
};
}  // namespace qingying
