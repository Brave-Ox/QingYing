#include "pipe_identity.h"
#include <gtest/gtest.h>
#include <sddl.h>

namespace qingying::ipc {
TEST(PipeIdentityTest, CurrentLoginAndSessionAreRequired) {
  detail::Identity identity;
  ASSERT_TRUE(detail::currentIdentity(identity));
  EXPECT_TRUE(detail::sameIdentity(identity, identity));
  auto other = identity;
  ++other.session;
  EXPECT_FALSE(detail::sameIdentity(identity, other));
  other = identity;
  other.logon_sid += L"-1";
  EXPECT_FALSE(detail::sameIdentity(identity, other));
  other = identity;
  other.user_sid += L"-1";
  EXPECT_FALSE(detail::sameIdentity(identity, other));
  EXPECT_FALSE(detail::sameIdentity({}, {}));
  EXPECT_FALSE(detail::verifyPeer(INVALID_HANDLE_VALUE, true, identity));
  EXPECT_FALSE(detail::verifyPeer(INVALID_HANDLE_VALUE, false, identity));
}
TEST(PipeIdentityTest, NamesAreLocalAndCannotEscapeTestNamespace) {
  detail::Identity identity;
  ASSERT_TRUE(detail::currentIdentity(identity));
  auto name = detail::pipeName(identity, L"");
  EXPECT_EQ(name, L"\\\\.\\pipe\\QingYing.Automation.v1." + identity.logon_sid);
  EXPECT_EQ(detail::pipeName(identity, L"case-1"), name + L".test.case-1");
  for (const auto* invalid : {L"..\\evil", L"\\\\remote\\pipe\\x", L"x/y", L"a.b"})
    EXPECT_TRUE(detail::pipeName(identity, invalid).empty());
  EXPECT_TRUE(detail::pipeName(identity, std::wstring(65, L'x')).empty());
}
TEST(PipeIdentityTest, ProtectedDaclHasOnlyLoginAceAndHandlesAreNotInherited) {
  detail::Identity identity;
  ASSERT_TRUE(detail::currentIdentity(identity));
  detail::PipeSecurity security(identity);
  ASSERT_NE(security.attributes(), nullptr);
  EXPECT_FALSE(security.attributes()->bInheritHandle);
  auto descriptor = security.attributes()->lpSecurityDescriptor;
  SECURITY_DESCRIPTOR_CONTROL control = 0;
  DWORD revision = 0;
  ASSERT_TRUE(GetSecurityDescriptorControl(descriptor, &control, &revision));
  EXPECT_TRUE(control & SE_DACL_PROTECTED);
  BOOL present = FALSE, defaulted = TRUE;
  PACL acl = nullptr;
  ASSERT_TRUE(GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted));
  ASSERT_TRUE(present);
  ASSERT_NE(acl, nullptr);
  EXPECT_FALSE(defaulted);
  ASSERT_EQ(acl->AceCount, 1);
  void* raw = nullptr;
  ASSERT_TRUE(GetAce(acl, 0, &raw));
  auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
  EXPECT_EQ(ace->Header.AceType, ACCESS_ALLOWED_ACE_TYPE);
  EXPECT_EQ(ace->Mask, 0x0012019fu);
  PSID expected = nullptr;
  ASSERT_TRUE(ConvertStringSidToSidW(identity.logon_sid.c_str(), &expected));
  EXPECT_TRUE(EqualSid(&ace->SidStart, expected));
  LocalFree(expected);
}
TEST(PipeIdentityTest, ExplicitDaclRejectsAnonymousTokenOnActualLocalPipe) {
  detail::Identity identity;
  ASSERT_TRUE(detail::currentIdentity(identity));
  detail::PipeSecurity security(identity);
  const auto name = detail::pipeName(identity, L"acl_" +
      std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()));
  detail::Handle server(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX |
      FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_REJECT_REMOTE_CLIENTS,
      1, 4096, 4096, 0, security.attributes()));
  ASSERT_TRUE(server);
  ASSERT_TRUE(ImpersonateAnonymousToken(GetCurrentThread()));
  const HANDLE raw = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
      nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
  const DWORD error = GetLastError();
  const BOOL reverted = RevertToSelf();
  detail::Handle client(raw);
  ASSERT_TRUE(reverted);
  EXPECT_FALSE(client);
  EXPECT_EQ(error, ERROR_ACCESS_DENIED);
}
}  // namespace qingying::ipc
