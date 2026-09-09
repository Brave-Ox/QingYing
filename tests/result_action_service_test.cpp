#include "qingying/app/result_action_service.h"
#include "qingying/export/export_service.hpp"
#include "qingying/app/save_policy.h"
#include "qingying/pin/pin_manager.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace qingying {
namespace {
struct TemporaryFile {
  std::wstring path;
  TemporaryFile() {
    wchar_t directory[MAX_PATH] = {}, file[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, directory) && GetTempFileNameW(directory, L"qy3", 0, file)) path = file;
  }
  ~TemporaryFile() { if (!path.empty()) DeleteFileW(path.c_str()); }
};
std::vector<char> readFile(const std::wstring& path) {
  std::ifstream stream(std::filesystem::path(path), std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
}
TEST(ResultActionServiceTest, ModalReentryExportsOriginalImageAfterClearAndReplacement) {
  ResultStore store;
  ExportService exporter;
  PinManager pins;
  TemporaryFile expected, actual;
  ASSERT_FALSE(expected.path.empty());
  ASSERT_FALSE(actual.path.empty());
  const Image original{2, 1, {0xFF112233u, 0xFF445566u}};
  ASSERT_TRUE(exporter.savePng(original, expected.path).ok);
  const auto id = store.publish(2, original);
  bool dialog_called = false;
  ResultActionService actions(store, exporter, pins, [&](HWND) {
    dialog_called = true;
    store.clearScope(2);
    store.publish(2, Image{1, 1, {0xFFFFFFFFu}});
    return std::optional<std::wstring>{actual.path};
  });
  EXPECT_TRUE(actions.save(2, ResultSelection::specific(id)).ok);
  EXPECT_TRUE(dialog_called);
  EXPECT_FALSE(store.acquire(2, id));
  EXPECT_FALSE(readFile(actual.path).empty());
  EXPECT_EQ(readFile(actual.path), readFile(expected.path));
}
TEST(ResultActionServiceTest, ForeignIdsAreRejectedBeforeSideEffects) {
  ResultStore store;
  ExportService exporter;
  PinManager pins;
  ResultActionService actions(store, exporter, pins);
  const auto id = store.publish(3, Image{1, 1, {1}});
  const auto selected = ResultSelection::specific(id);
  EXPECT_FALSE(actions.copy(2, selected).ok);
  EXPECT_FALSE(actions.save(2, selected, L"unused.png").ok);
  EXPECT_FALSE(actions.pin(2, selected).ok);
  EXPECT_TRUE(store.acquire(3, id));
}

TEST(ResultActionServiceTest, ExternalCopyCommitsOnceAndKeepsResultConsumable) {
  ResultStore store;
  ExportService exporter;
  PinManager pins;
  int clipboard_writes = 0;
  ResultActionService actions(store, exporter, pins, {}, nullptr, nullptr, {},
      [&](const Image& image) {
        ++clipboard_writes;
        ActionResult result;
        result.ok = !image.empty();
        result.error_code = result.ok ? ErrorCode::kOk
                                      : ErrorCode::kExportFailed;
        return result;
      });
  constexpr ResultScopeId scope = kGuiResultScopeId;
  const auto id = store.publish(scope, Image{1, 1, {0xFF010203u}});

  int commit_checks = 0;
  auto copied = actions.copy(scope, ResultSelection::specific(id), nullptr,
      [&] { ++commit_checks; return true; });
  ASSERT_TRUE(copied.ok);
  EXPECT_EQ(commit_checks, 1);
  EXPECT_EQ(clipboard_writes, 1);
  ASSERT_NE(std::get_if<CopiedResult>(&copied.output), nullptr);
  EXPECT_EQ(std::get<CopiedResult>(copied.output).result_id, id);
  EXPECT_TRUE(store.acquire(scope, ResultSelection::specific(id)));
  TemporaryFile saved;
  ASSERT_FALSE(saved.path.empty());
  EXPECT_TRUE(actions.save(id, saved.path).ok);
  EXPECT_TRUE(store.acquire(scope, ResultSelection::specific(id)));
}

TEST(ResultActionServiceTest, ExternalCopyCancellationPreventsClipboardCommit) {
  ResultStore store;
  ExportService exporter;
  PinManager pins;
  int clipboard_writes = 0;
  ResultActionService actions(store, exporter, pins, {}, nullptr, nullptr, {},
      [&](const Image&) {
        ++clipboard_writes;
        ActionResult result;
        result.ok = true;
        result.error_code = ErrorCode::kOk;
        return result;
      });
  constexpr ResultScopeId scope = 8;
  const auto id = store.publish(scope, Image{1, 1, {0xFF010203u}});

  const auto cancelled = actions.copy(scope, ResultSelection::specific(id),
                                      nullptr, [] { return false; });
  EXPECT_EQ(cancelled.error_code, ErrorCode::kCancelled);
  EXPECT_EQ(clipboard_writes, 0);
  EXPECT_TRUE(store.acquire(scope, ResultSelection::specific(id)));
}

TEST(ResultActionServiceTest, ExpiredExternalCopyIsRejectedBeforeClipboardWrite) {
  auto now = std::chrono::steady_clock::now();
  ResultStore store({}, [&] { return now; });
  ExportService exporter;
  PinManager pins;
  int clipboard_writes = 0;
  ResultActionService actions(store, exporter, pins, {}, nullptr, nullptr, {},
      [&](const Image&) {
        ++clipboard_writes;
        ActionResult result;
        result.ok = true;
        result.error_code = ErrorCode::kOk;
        return result;
      });
  constexpr ResultScopeId scope = 10;
  const auto id = store.publish(scope, Image{1, 1, {0xFF010203u}});
  now += std::chrono::seconds{61};

  const auto expired = actions.copyAdmitted(scope,
      ResultSelection::specific(id), [] { return true; });
  EXPECT_EQ(expired.error_code, ErrorCode::kResultExpired);
  EXPECT_EQ(clipboard_writes, 0);
}
TEST(ResultActionServiceTest, CancelledDialogAllowsReentrantClearWithoutWriting) {
  ResultStore store;
  ExportService exporter;
  PinManager pins;
  const auto id = store.publish(Image{1, 1, {1}});
  ResultActionService actions(store, exporter, pins, [&](HWND) {
    store.clear();
    return std::optional<std::wstring>{};
  });
  EXPECT_TRUE(actions.save(id).ok);
  EXPECT_EQ(store.currentId(), kInvalidResultId);
}
TEST(ResultActionServiceTest, ExternalSaveUsesPolicyCommitAndTypedMetadata) {
  wchar_t directory[MAX_PATH] = {};
  wchar_t seed[MAX_PATH] = {};
  ASSERT_NE(GetTempPathW(MAX_PATH, directory), 0u);
  ASSERT_NE(GetTempFileNameW(directory, L"qra", 0, seed), 0u);
  DeleteFileW(seed);
  ASSERT_TRUE(CreateDirectoryW(seed, nullptr));
  const std::filesystem::path root(seed);
  const auto cancelled_path = root / L"cancelled.png";
  const auto saved_path = root / L"saved.png";

  ResultStore store;
  ExportService exporter;
  PinManager pins;
  SavePolicy policy({root.wstring()});
  ResultActionService actions(store, exporter, pins, {}, nullptr, &policy);
  constexpr ResultScopeId scope = 7;
  const auto id = store.publish(scope, Image{1, 1, {0xFF010203u}});

  bool cancellation_checked = false;
  auto cancelled = actions.save(scope, ResultSelection::specific(id),
      cancelled_path.wstring(), nullptr, [&] {
        cancellation_checked = true;
        return false;
      });
  EXPECT_TRUE(cancellation_checked);
  EXPECT_EQ(cancelled.error_code, ErrorCode::kCancelled);
  EXPECT_FALSE(std::filesystem::exists(cancelled_path));

  auto saved = actions.save(scope, ResultSelection::specific(id),
      saved_path.wstring(), nullptr, [] { return true; });
  ASSERT_TRUE(saved.ok) << saved.message;
  const auto* metadata = std::get_if<SavedResult>(&saved.output);
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->result_id, id);
  EXPECT_EQ(metadata->absolute_path, saved_path.wstring());
  EXPECT_EQ(metadata->format, ImageFormat::Png);

  std::filesystem::remove_all(root);
}
}  // namespace qingying
