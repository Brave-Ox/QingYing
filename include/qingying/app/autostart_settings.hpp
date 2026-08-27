#pragma once

namespace qingying {

// HKCU Run key helper for "Start with Windows".
class AutostartSettings {
 public:
  static bool isEnabled();
  static bool setEnabled(bool enabled);
};

}  // namespace qingying
