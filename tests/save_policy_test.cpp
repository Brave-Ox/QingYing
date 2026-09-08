#include "qingying/app/save_policy.h"

#include <Windows.h>
#include <winioctl.h>

#include <array>
#include <cstring>
#include <filesystem>

#include <gtest/gtest.h>

namespace qingying {
namespace {

class TemporaryDirectory final {
 public:
  TemporaryDirectory() {
    wchar_t base[MAX_PATH] = {};
    wchar_t seed[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, base) == 0 ||
        GetTempFileNameW(base, L"qsp", 0, seed) == 0) return;
    DeleteFileW(seed);
    if (CreateDirectoryW(seed, nullptr)) path_ = seed;
  }
  ~TemporaryDirectory() {
    if (!path_.empty()) std::filesystem::remove_all(path_);
  }
  const std::wstring& path() const noexcept { return path_; }
 private:
  std::wstring path_;
};

bool createJunction(const std::filesystem::path& link,
                    const std::filesystem::path& target) {
  struct MountPointReparseData {
    ULONG tag;
    USHORT data_length;
    USHORT reserved;
    USHORT substitute_offset;
    USHORT substitute_length;
    USHORT print_offset;
    USHORT print_length;
    wchar_t paths[1];
  };
  if (!CreateDirectoryW(link.c_str(), nullptr)) return false;
  HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  const std::wstring substitute = L"\\??\\" + target.wstring();
  const std::wstring print = target.wstring();
  std::array<std::byte, MAXIMUM_REPARSE_DATA_BUFFER_SIZE> storage{};
  auto* buffer = reinterpret_cast<MountPointReparseData*>(storage.data());
  buffer->tag = IO_REPARSE_TAG_MOUNT_POINT;
  buffer->substitute_offset = 0;
  buffer->substitute_length =
      static_cast<USHORT>(substitute.size() * sizeof(wchar_t));
  buffer->print_offset = buffer->substitute_length + sizeof(wchar_t);
  buffer->print_length =
      static_cast<USHORT>(print.size() * sizeof(wchar_t));
  std::memcpy(buffer->paths, substitute.c_str(),
              (substitute.size() + 1) * sizeof(wchar_t));
  std::memcpy(reinterpret_cast<std::byte*>(buffer->paths) + buffer->print_offset,
              print.c_str(), (print.size() + 1) * sizeof(wchar_t));
  buffer->data_length = static_cast<USHORT>(
      8 + buffer->print_offset + buffer->print_length + sizeof(wchar_t));
  DWORD returned = 0;
  const bool ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer,
      8 + buffer->data_length, nullptr, 0,
      &returned, nullptr) != FALSE;
  CloseHandle(handle);
  return ok;
}

TEST(SavePolicyTest, AcceptsUnicodePngBelowConfiguredLocalRoot) {
  TemporaryDirectory root;
  ASSERT_FALSE(root.path().empty());
  const auto child = std::filesystem::path(root.path()) / L"截图";
  ASSERT_TRUE(CreateDirectoryW(child.c_str(), nullptr));
  SavePolicy policy({root.path()});
  ValidatedSavePath output;
  const auto result = policy.validate(child.wstring(), L"结果.png", false, &output);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(output.absolute_path, (child / L"结果.png").wstring());
  EXPECT_FALSE(output.overwrite);
}

TEST(SavePolicyTest, RejectsTraversalUncDeviceAdsAndReservedNames) {
  TemporaryDirectory root;
  TemporaryDirectory outside;
  ASSERT_FALSE(root.path().empty());
  ASSERT_FALSE(outside.path().empty());
  SavePolicy policy({root.path()});
  ValidatedSavePath output;
  EXPECT_EQ(policy.validate(outside.path(), L"x.png", false, &output).error_code,
            ErrorCode::kAccessDenied);
  EXPECT_EQ(policy.validate(root.path(), L"..\\x.png", false, &output).error_code,
            ErrorCode::kInvalidArgument);
  EXPECT_EQ(policy.validate(root.path(), L"CON.png", false, &output).error_code,
            ErrorCode::kInvalidArgument);
  EXPECT_EQ(policy.validate(root.path(), L"x.txt", false, &output).error_code,
            ErrorCode::kInvalidArgument);
  EXPECT_EQ(policy.validateFullPath(L"\\\\server\\share\\x.png", false, &output).error_code,
            ErrorCode::kAccessDenied);
  EXPECT_EQ(policy.validateFullPath(root.path() + L"\\x.png:stream", false, &output).error_code,
            ErrorCode::kAccessDenied);
  EXPECT_EQ(policy.validateFullPath(L"\\\\?\\C:\\x.png", false, &output).error_code,
            ErrorCode::kAccessDenied);
}

TEST(SavePolicyTest, RejectsReparseDirectoryInAllowedTree) {
  TemporaryDirectory root;
  TemporaryDirectory target;
  ASSERT_FALSE(root.path().empty());
  ASSERT_FALSE(target.path().empty());
  const auto link = std::filesystem::path(root.path()) / L"linked";
  ASSERT_TRUE(createJunction(link, target.path()));
  SavePolicy policy({root.path()});
  ValidatedSavePath output;
  EXPECT_EQ(policy.validate(link.wstring(), L"x.png", false, &output).error_code,
            ErrorCode::kAccessDenied);
}

}  // namespace
}  // namespace qingying
