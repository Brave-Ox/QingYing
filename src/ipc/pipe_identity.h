#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <string>
#include <vector>

namespace qingying::ipc::detail {
class Handle final {
 public:
  explicit Handle(HANDLE value = nullptr) : value_(value) {}
  ~Handle() {
    const DWORD error = GetLastError();
    reset();
    SetLastError(error);
  }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  HANDLE get() const { return value_; }
  explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
  void reset(HANDLE value = nullptr) {
    if (*this) CloseHandle(value_);
    value_ = value;
  }
 private:
  HANDLE value_;
};
struct Identity {
  std::wstring logon_sid;
  std::wstring user_sid;
  DWORD session{0};
};
bool tokenIdentity(HANDLE token, Identity& result);
bool currentIdentity(Identity& result);
bool sameIdentity(const Identity& expected, const Identity& actual);
bool verifyPeer(HANDLE pipe, bool server_end, const Identity& expected);
std::wstring pipeName(const Identity& identity, const std::wstring& test_suffix);
// Owns an explicit protected DACL from the first instance onwards.
class PipeSecurity final {
 public:
  explicit PipeSecurity(const Identity& identity);
  ~PipeSecurity();
  PipeSecurity(const PipeSecurity&) = delete;
  PipeSecurity& operator=(const PipeSecurity&) = delete;
  SECURITY_ATTRIBUTES* attributes() { return descriptor_ ? &attributes_ : nullptr; }
 private:
  PSECURITY_DESCRIPTOR descriptor_{nullptr};
  SECURITY_ATTRIBUTES attributes_{};
};
}  // namespace qingying::ipc::detail
