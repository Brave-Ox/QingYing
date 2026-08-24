#pragma once

#include <Windows.h>

namespace qingying {

// Named mutex single-instance guard (Local\QingYing.SingleInstance).
class SingleInstanceGuard {
 public:
  SingleInstanceGuard();
  ~SingleInstanceGuard();

  SingleInstanceGuard(const SingleInstanceGuard&) = delete;
  SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

  bool acquired() const { return acquired_; }

 private:
  HANDLE mutex_{nullptr};
  bool acquired_{false};
};

}  // namespace qingying
