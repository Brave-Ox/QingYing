#include "qingying/export/export_service.hpp"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <gtest/gtest.h>

namespace {

class TemporaryPngPath {
 public:
  TemporaryPngPath() {
    wchar_t directory[MAX_PATH] = {};
    const DWORD directory_length =
        GetTempPathW(static_cast<DWORD>(std::size(directory)), directory);
    if (directory_length == 0 || directory_length >= std::size(directory)) {
      return;
    }

    wchar_t temporary_name[MAX_PATH] = {};
    if (GetTempFileNameW(directory, L"qyp", 0, temporary_name) == 0) {
      return;
    }
    DeleteFileW(temporary_name);
    path_ = std::wstring(temporary_name) + L".png";
  }

  ~TemporaryPngPath() {
    if (!path_.empty()) {
      DeleteFileW(path_.c_str());
    }
  }

  TemporaryPngPath(const TemporaryPngPath&) = delete;
  TemporaryPngPath& operator=(const TemporaryPngPath&) = delete;

  const std::wstring& path() const { return path_; }

 private:
  std::wstring path_;
};

}  // namespace

TEST(ExportServiceTest, SavePngRejectsEmptyImage) {
  qingying::ExportService service;
  const qingying::ActionResult result =
      service.savePng(qingying::Image{}, L"qingying-empty.png");

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kExportFailed);
}

TEST(ExportServiceTest, SavePngRejectsEmptyPath) {
  qingying::ExportService service;
  qingying::Image image;
  image.width = 1;
  image.height = 1;
  image.pixels = {0xFF112233u};

  const qingying::ActionResult result = service.savePng(image, L"");

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kInvalidArgument);
}

TEST(ExportServiceTest, SavePngWritesPngFile) {
  TemporaryPngPath output;
  ASSERT_FALSE(output.path().empty());

  qingying::Image image;
  image.width = 2;
  image.height = 2;
  image.pixels = {
      0xFFFF0000u, 0xFF00FF00u,
      0xFF0000FFu, 0xFFFFFFFFu,
  };

  qingying::ExportService service;
  const qingying::ActionResult result =
      service.savePng(image, output.path());

  ASSERT_TRUE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kOk);

  std::ifstream file(std::filesystem::path(output.path()),
                     std::ios::in | std::ios::binary);
  ASSERT_TRUE(file.is_open());

  std::array<std::uint8_t, 8> signature{};
  file.read(reinterpret_cast<char*>(signature.data()),
            static_cast<std::streamsize>(signature.size()));
  ASSERT_EQ(file.gcount(), static_cast<std::streamsize>(signature.size()));

  const std::array<std::uint8_t, 8> expected_signature = {
      0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au};
  EXPECT_EQ(signature, expected_signature);
}
