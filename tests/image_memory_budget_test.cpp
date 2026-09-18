#include "qingying/action/image.hpp"
#include "qingying/app/result_store.h"
#include "qingying/app/export_executor.h"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/dib_encoder.hpp"
#include "qingying/longshot/image_stitcher.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <future>
#include <thread>

namespace qingying {
namespace {
constexpr std::size_t kindIndex(ImageMemoryKind kind) { return static_cast<std::size_t>(kind); }
class MemoryLimit {
 public:
  explicit MemoryLimit(std::uint64_t extra) : old_(budget_.snapshot().limit_bytes) {
    if (!budget_.setLimit(budget_.snapshot().used_bytes + extra)) throw std::runtime_error("test budget limit");
  }
  ~MemoryLimit() { budget_.setLimit(old_); }
 private:
  ImageMemoryBudget budget_{ImageMemoryBudget::global()};
  std::uint64_t old_;
};
}
TEST(ImageMemoryBudgetTest, TokensShareCeilingAndReturnCapacityAfterMoveAndCancellation) {
  ImageMemoryBudget budget{64};
  auto capture = budget.reserve(32, ImageMemoryKind::InFlight);
  auto encode = budget.reserve(32, ImageMemoryKind::EncodeScratch);
  ASSERT_TRUE(capture); ASSERT_TRUE(encode);
  EXPECT_FALSE(budget.reserve(1, ImageMemoryKind::Preview));
  auto moved = std::move(capture);
  EXPECT_FALSE(capture);
  encode.reset(); moved.reset();
  const auto metrics = budget.snapshot();
  EXPECT_EQ(metrics.used_bytes, 0u);
  EXPECT_EQ(metrics.peak_bytes, 64u);
  EXPECT_EQ(metrics.rejected, 1u);
  EXPECT_TRUE(budget.reserve(64, ImageMemoryKind::Preview));
}
TEST(ImageMemoryBudgetTest, ConcurrentTokensNeverExceedTheSharedLimit) {
  ImageMemoryBudget budget{64};
  std::vector<std::thread> workers;
  for (int i = 0; i < 8; ++i) workers.emplace_back([budget] {
    for (int j = 0; j < 1000; ++j) {
      auto token = budget.reserve(16, ImageMemoryKind::InFlight);
      std::this_thread::yield();
    }
  });
  for (auto& worker : workers) worker.join();
  const auto metrics = budget.snapshot();
  EXPECT_EQ(metrics.used_bytes, 0u);
  EXPECT_LE(metrics.peak_bytes, 64u);
}
TEST(ImageMemoryBudgetTest, PixelCopiesAreAdmittedAndMovesDoNotAllocateAgain) {
  const auto baseline = ImageMemoryBudget::global().snapshot().used_bytes;
  MemoryLimit limit{64};
  Image source{8, 1, {1, 2, 3, 4, 5, 6, 7, 8}};
  Image copy = source;
  EXPECT_EQ(ImageMemoryBudget::global().snapshot().used_bytes, baseline + 64);
  EXPECT_THROW((void)Image(source), std::bad_alloc);
  auto* storage = copy.pixels.data();
  Image moved = std::move(copy);
  EXPECT_EQ(moved.pixels.data(), storage);
  EXPECT_EQ(ImageMemoryBudget::global().snapshot().used_bytes, baseline + 64);
  EXPECT_EQ(ImageMemoryBudget::global().snapshot().bytes[kindIndex(ImageMemoryKind::WireCopy)], 32u);
}
TEST(ImageMemoryBudgetTest, GrowthChargesOldAndNewCapacityAndPreservesPixelsOnRejection) {
  MemoryLimit limit{64};
  Image image{8, 1, {1, 2, 3, 4, 5, 6, 7, 8}};
  EXPECT_THROW(image.pixels.reserve(16), std::bad_alloc);
  EXPECT_EQ(image.pixels.capacity(), 8u);
  EXPECT_EQ(image.pixels[0], 1u);
  EXPECT_TRUE(ImageMemoryBudget::global().setLimit(96));
  image.pixels.reserve(16);
  EXPECT_EQ(image.pixels.capacity(), 16u);
  EXPECT_EQ(ImageMemoryBudget::global().snapshot().used_bytes, 64u);
}
TEST(ImageMemoryBudgetTest, LeasesSharePixelsAndDetachedLeasesStayCharged) {
  const auto baseline = ImageMemoryBudget::global().snapshot().used_bytes;
  MemoryLimit limit{64};
  ResultStore store;
  auto id = store.publish(2, Image{4, 1, {1, 2, 3, 4}});
  auto a = store.acquire(2, id), b = store.acquire(2, id);
  ASSERT_TRUE(a); ASSERT_TRUE(b);
  EXPECT_EQ(a.image(), b.image());
  EXPECT_EQ(ImageMemoryBudget::global().snapshot().used_bytes, baseline + 16);
  store.clearScope(2);
  EXPECT_EQ(ImageMemoryBudget::global().snapshot().bytes[kindIndex(ImageMemoryKind::ExternalLease)], 16u);
  a = {}; EXPECT_EQ(ImageMemoryBudget::global().snapshot().used_bytes, baseline + 16);
  b = {}; EXPECT_EQ(ImageMemoryBudget::global().snapshot().used_bytes, baseline);
}
TEST(ImageMemoryBudgetTest, LegacyGetCopyIsBudgetedAndFailsBeforeCopyingPixels) {
  MemoryLimit limit{32};
  ResultStore store;
  const auto id = store.publish(Image{4, 1, {1, 2, 3, 4}});
  ASSERT_NE(id, kInvalidResultId);
  auto copy = store.get(id);
  ASSERT_TRUE(copy);
  EXPECT_THROW(store.get(id), std::bad_alloc);
  copy.reset();
  EXPECT_TRUE(store.get(id));
}
TEST(ImageMemoryBudgetTest, LongshotMergeCountsAllFramesAndRejectsWithoutChangingOutput) {
  MemoryLimit limit{32};
  Image accumulated{2, 2, {1, 2, 3, 4}};
  Image next{2, 2, {5, 6, 7, 8}};
  ImageStitcher stitcher;
  EXPECT_THROW(stitcher.append(accumulated, next), std::bad_alloc);
  EXPECT_EQ(accumulated.height, 2);
  EXPECT_EQ(accumulated.pixels.back(), 4u);
}
TEST(ImageMemoryBudgetTest, CaptureRejectsNativeBitmapAndDibCopyBeforeAllocating) {
  MemoryLimit limit{4};
  CaptureEngine engine;
  Image image;
  const auto result = engine.captureRegion(0, 0, 2, 2, image);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, ErrorCode::kResourceLimit);
  EXPECT_TRUE(image.empty());
}
TEST(ImageMemoryBudgetTest, DibOutputKeepsScratchChargedUntilBufferDestruction) {
  const auto baseline = ImageMemoryBudget::global().snapshot().used_bytes;
  MemoryLimit limit{64};
  Image image{1, 1, {1}};
  {
    const auto bytes = dib::encodeDib(image);
    EXPECT_EQ(bytes.size(), 44u);
    EXPECT_EQ(ImageMemoryBudget::global().snapshot().used_bytes, baseline + 48);
    EXPECT_THROW(dib::encodeDib(image), std::bad_alloc);
  }
  EXPECT_EQ(ImageMemoryBudget::global().snapshot().used_bytes, baseline + 4);
}
TEST(ImageMemoryBudgetTest, PreviewCountAndBytesHaveIndependentLimits) {
  ImageMemoryBudget budget{64ULL * 1024 * 1024};
  std::vector<ImageMemoryBudget::Token> tokens;
  for (int i = 0; i < 4; ++i) tokens.push_back(budget.reserve(4, ImageMemoryKind::Preview));
  EXPECT_FALSE(budget.reserve(4, ImageMemoryKind::Preview));
  tokens.clear();
  EXPECT_FALSE(budget.reserve(16ULL * 1024 * 1024 + 1, ImageMemoryKind::Preview));
  EXPECT_EQ(budget.snapshot().used_bytes, 0u);
}
TEST(ImageMemoryBudgetTest, CancelledExportQueueReturnsCapturedPixelAndJobTokens) {
  const auto baseline = ImageMemoryBudget::global().snapshot().used_bytes;
  ExportExecutor executor{8};
  std::promise<void> started, release;
  auto released = release.get_future().share();
  ASSERT_TRUE(executor.submit([&] { started.set_value(); released.wait(); }));
  started.get_future().wait();
  Image image{4, 1, {1, 2, 3, 4}};
  std::atomic<int> rejected{0};
  EXPECT_TRUE(executor.submit([pixels = image] {}, [&] { ++rejected; }));
  EXPECT_GT(ImageMemoryBudget::global().snapshot().bytes[kindIndex(ImageMemoryKind::WorkerQueue)], 0u);
  executor.requestStop();
  EXPECT_EQ(rejected, 1);
  release.set_value(); executor.shutdown();
  EXPECT_EQ(ImageMemoryBudget::global().snapshot().used_bytes, baseline + 16);
}
}  // namespace qingying
