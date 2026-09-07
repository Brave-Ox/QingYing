#include "qingying/app/result_action_service.h"
#include "qingying/export/export_service.hpp"
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
}  // namespace qingying
