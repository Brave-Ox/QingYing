#include "pipe_identity.h"

#include <sddl.h>

namespace qingying::ipc::detail {
namespace {
bool tokenData(HANDLE token, TOKEN_INFORMATION_CLASS kind, std::vector<BYTE>& data) {
  DWORD size = 0;
  GetTokenInformation(token, kind, nullptr, 0, &size);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !size) return false;
  data.resize(size);
  return GetTokenInformation(token, kind, data.data(), size, &size) != FALSE;
}
std::wstring sidText(PSID sid) {
  LPWSTR text = nullptr;
  if (!IsValidSid(sid) || !ConvertSidToStringSidW(sid, &text)) return {};
  std::wstring result(text);
  LocalFree(text);
  return result;
}
}
bool tokenIdentity(HANDLE token, Identity& result) {
  Identity identity;
  std::vector<BYTE> data;
  if (!tokenData(token, TokenUser, data)) return false;
  identity.user_sid = sidText(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid);
  if (!tokenData(token, TokenGroups, data)) return false;
  const auto* groups = reinterpret_cast<TOKEN_GROUPS*>(data.data());
  for (DWORD i = 0; i < groups->GroupCount; ++i) {
    const auto& group = groups->Groups[i];
    if ((group.Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID)
      identity.logon_sid = sidText(group.Sid);
  }
  DWORD size = 0;
  if (!GetTokenInformation(token, TokenSessionId, &identity.session,
                           sizeof(identity.session), &size) ||
      identity.logon_sid.empty() || identity.user_sid.empty()) return false;
  result = std::move(identity);
  return true;
}
bool currentIdentity(Identity& result) {
  HANDLE raw = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) return false;
  Handle token(raw);
  return tokenIdentity(token.get(), result);
}
bool sameIdentity(const Identity& expected, const Identity& actual) {
  return !expected.logon_sid.empty() && !expected.user_sid.empty() &&
      expected.logon_sid == actual.logon_sid &&
      expected.user_sid == actual.user_sid && expected.session == actual.session;
}
bool verifyPeer(HANDLE pipe, bool server_end, const Identity& expected) {
  ULONG pid = 0;
  ULONG session = 0;
  if (!(server_end ? GetNamedPipeClientProcessId(pipe, &pid)
                   : GetNamedPipeServerProcessId(pipe, &pid)) ||
      !(server_end ? GetNamedPipeClientSessionId(pipe, &session)
                   : GetNamedPipeServerSessionId(pipe, &session)) ||
      session != expected.session) return false;
  Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
  HANDLE raw = nullptr;
  if (!process || !OpenProcessToken(process.get(), TOKEN_QUERY, &raw)) return false;
  Handle token(raw);
  Identity actual;
  if (!tokenIdentity(token.get(), actual) || !sameIdentity(expected, actual)) return false;
  // Verify the effective client token too: a process can open a pipe while
  // impersonating another login. This runs only after reading its hello.
  if (server_end) {
    if (!ImpersonateNamedPipeClient(pipe)) return false;
    struct Revert {
      ~Revert() { if (!RevertToSelf()) std::terminate(); }
    } revert;
    raw = nullptr;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &raw)) return false;
    Handle effective(raw);
    Identity effective_identity;
    if (!tokenIdentity(effective.get(), effective_identity) ||
        !sameIdentity(expected, effective_identity)) return false;
  }
  return true;
}
std::wstring pipeName(const Identity& identity, const std::wstring& suffix) {
  if (identity.logon_sid.empty() || suffix.size() > 64) return {};
  for (wchar_t c : suffix) {
    if (!((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') ||
          (c >= L'a' && c <= L'z') || c == L'-' || c == L'_')) return {};
  }
  return L"\\\\.\\pipe\\QingYing.Automation.v1." + identity.logon_sid +
      (suffix.empty() ? L"" : L".test." + suffix);
}
PipeSecurity::PipeSecurity(const Identity& identity) {
  // The same login also creates subsequent server instances (FILE_APPEND_DATA
  // is FILE_CREATE_PIPE_INSTANCE). No Everyone/Admin/System fallback ACE.
  const std::wstring sddl = L"D:P(A;;0x0012019f;;;" + identity.logon_sid + L")";
  if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),
      SDDL_REVISION_1, &descriptor_, nullptr)) {
    attributes_.nLength = sizeof(attributes_);
    attributes_.lpSecurityDescriptor = descriptor_;
    attributes_.bInheritHandle = FALSE;
  }
}
PipeSecurity::~PipeSecurity() { if (descriptor_) LocalFree(descriptor_); }
}  // namespace qingying::ipc::detail
