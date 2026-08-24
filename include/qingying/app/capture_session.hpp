#pragma once

namespace qingying {

// Holds the latest capture result between CaptureRegion and Copy/Save handlers.
class CaptureSession {
 public:
  bool hasResult() const { return has_result_; }

  void markCaptured() { has_result_ = true; }
  void clear() { has_result_ = false; }

 private:
  bool has_result_{false};
};

}  // namespace qingying
