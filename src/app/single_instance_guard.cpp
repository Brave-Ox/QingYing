#include "qingying/app/single_instance_guard.hpp"

namespace qingying {

namespace {
constexpr wchar_t kMutexName[] = L"Local\\QingYing.SingleInstance";
}

SingleInstanceGuard::SingleInstanceGuard() {
  mutex_ = CreateMutexW(nullptr, TRUE, kMutexName);
  if (mutex_ == nullptr) {
    acquired_ = false;
    return;
  }
  acquired_ = (GetLastError() != ERROR_ALREADY_EXISTS);
}

SingleInstanceGuard::~SingleInstanceGuard() {
  if (mutex_ != nullptr) {
    if (acquired_) {
      ReleaseMutex(mutex_);
    }
    CloseHandle(mutex_);
    mutex_ = nullptr;
  }
}

}  // namespace qingying
