#include "export/png_file_transaction.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include <gtest/gtest.h>

namespace qingying::detail {
namespace {

class TemporaryDirectory final {
 public:
  TemporaryDirectory() {
    wchar_t base[MAX_PATH] = {};
    wchar_t seed[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, base) == 0 ||
        GetTempFileNameW(base, L"qpt", 0, seed) == 0) return;
    DeleteFileW(seed);
    if (CreateDirectoryW(seed, nullptr)) path_ = seed;
  }
  ~TemporaryDirectory() {
    if (!path_.empty()) std::filesystem::remove_all(path_);
  }
  std::filesystem::path file(const wchar_t* name) const { return path_ / name; }
  const std::filesystem::path& path() const noexcept { return path_; }
 private:
  std::filesystem::path path_;
};

Image image() { return Image{1, 1, {0xFF123456u}}; }

std::vector<char> read(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), {}};
}

std::size_t temporaryCount(const std::filesystem::path& directory) {
  std::size_t count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (entry.path().filename().wstring().rfind(L".qingying-", 0) == 0) ++count;
  }
  return count;
}

TEST(PngFileTransactionTest, DefaultRefusesCompetingDestinationAndCleansTemporary) {
  TemporaryDirectory directory;
  const auto destination = directory.file(L"race.png");
  ExportService::PngSaveOptions options;
  options.authorize_commit = [&] {
    std::ofstream(destination, std::ios::binary) << "competitor";
    return true;
  };
  const auto result = savePngFileTransaction(image(), destination.wstring(), options);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, ErrorCode::kConflict);
  EXPECT_EQ(read(destination), std::vector<char>({'c','o','m','p','e','t','i','t','o','r'}));
  EXPECT_EQ(temporaryCount(directory.path()), 0u);
}

TEST(PngFileTransactionTest, ExplicitOverwriteAtomicallyReplacesExistingFile) {
  TemporaryDirectory directory;
  const auto destination = directory.file(L"replace.png");
  std::ofstream(destination, std::ios::binary) << "old";
  ExportService::PngSaveOptions options;
  options.overwrite = true;
  const auto result = savePngFileTransaction(image(), destination.wstring(), options);
  ASSERT_TRUE(result.ok) << result.message;
  const auto bytes = read(destination);
  ASSERT_GE(bytes.size(), 8u);
  EXPECT_EQ(static_cast<unsigned char>(bytes[0]), 0x89u);
  EXPECT_EQ(temporaryCount(directory.path()), 0u);
}

TEST(PngFileTransactionTest, CancellationBeforeCommitLeavesNoOutputOrTemporary) {
  TemporaryDirectory directory;
  const auto destination = directory.file(L"cancelled.png");
  ExportService::PngSaveOptions options;
  const auto before = ImageMemoryBudget::global().snapshot().used_bytes;
  options.authorize_commit = [] {
    EXPECT_GT(ImageMemoryBudget::global().snapshot().bytes[
        static_cast<std::size_t>(ImageMemoryKind::EncodeScratch)], 0u);
    return false;
  };
  const auto result = savePngFileTransaction(image(), destination.wstring(), options);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, ErrorCode::kCancelled);
  EXPECT_FALSE(std::filesystem::exists(destination));
  EXPECT_EQ(temporaryCount(directory.path()), 0u);
  EXPECT_EQ(ImageMemoryBudget::global().snapshot().used_bytes, before);
}

TEST(PngFileTransactionTest, BudgetExhaustionCreatesNoFileAndRetryCanCommit) {
  TemporaryDirectory directory;
  const auto destination = directory.file(L"budget.png");
  auto pixels = image();
  auto budget = ImageMemoryBudget::global();
  const auto before = budget.snapshot();
  struct RestoreLimit { ImageMemoryBudget budget; std::uint64_t bytes; ~RestoreLimit() { budget.setLimit(bytes); } } restore{budget, before.limit_bytes};
  ASSERT_TRUE(budget.setLimit(before.used_bytes + 65535));
  const auto rejected = savePngFileTransaction(pixels, destination.wstring(), {});
  EXPECT_EQ(rejected.error_code, ErrorCode::kResourceLimit);
  EXPECT_FALSE(std::filesystem::exists(destination));
  EXPECT_EQ(temporaryCount(directory.path()), 0u);
  EXPECT_EQ(budget.snapshot().used_bytes, before.used_bytes);
  ASSERT_TRUE(budget.setLimit(before.limit_bytes));
  const auto retry = savePngFileTransaction(pixels, destination.wstring(), {});
  EXPECT_TRUE(retry.ok) << retry.message;
  EXPECT_EQ(budget.snapshot().used_bytes, before.used_bytes);
}

TEST(PngFileTransactionTest, DirectoryCannotBeReplacedDuringCommitAuthorization) {
  TemporaryDirectory parent;
  const auto directory = parent.file(L"stable");
  ASSERT_TRUE(CreateDirectoryW(directory.c_str(), nullptr));
  const auto destination = directory / L"result.png";
  bool replacement_blocked = false;
  ExportService::PngSaveOptions options;
  options.authorize_commit = [&] {
    const auto moved = parent.file(L"moved");
    replacement_blocked = MoveFileW(directory.c_str(), moved.c_str()) == FALSE;
    return true;
  };
  const auto result = savePngFileTransaction(image(), destination.wstring(), options);
  EXPECT_TRUE(replacement_blocked);
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(std::filesystem::exists(destination));
}

}  // namespace
}  // namespace qingying::detail
