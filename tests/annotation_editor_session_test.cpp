#include "qingying/annotate/annotation_editor_session.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace qingying {
namespace {

constexpr int kCanvasWidth = 40;
constexpr int kCanvasHeight = 30;
constexpr std::uint32_t kWhitePx = 0xFFFFFFFFu;
constexpr std::uint32_t kBlackPx = 0xFF000000u;
constexpr std::uint32_t kRedPx = 0xFFFF0000u;

Image makeCanvas()
{
  Image image;
  image.width = kCanvasWidth;
  image.height = kCanvasHeight;
  image.pixels.assign(static_cast<std::size_t>(kCanvasWidth) *
                          static_cast<std::size_t>(kCanvasHeight),
                      kWhitePx);
  return image;
}

// 左上 (10, 8)，尺寸 16x12 的红色描边矩形。
Annotation makeRedRectangle()
{
  Annotation annotation;
  annotation.type = AnnotationType::Rectangle;
  annotation.bounds.x = 10.0f;
  annotation.bounds.y = 8.0f;
  annotation.bounds.width = 16.0f;
  annotation.bounds.height = 12.0f;
  annotation.style.color = ColorBgra{0, 0, 255, 255};
  annotation.style.stroke_width = 2.0f;
  return annotation;
}

std::uint32_t pixelAt(const Image& image, int x, int y)
{
  return image.pixels.at(static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(image.width) +
                         static_cast<std::size_t>(x));
}

}  // namespace

TEST(AnnotationEditorSessionTest, NewSessionIsInactive)
{
  const AnnotationEditorSession session;
  EXPECT_FALSE(session.isActive());
  EXPECT_TRUE(session.source().empty());
}

TEST(AnnotationEditorSessionTest, BeginRejectsEmptySource)
{
  AnnotationEditorSession session;
  const Image empty;

  EXPECT_FALSE(session.begin(empty));
  EXPECT_FALSE(session.isActive());
}

TEST(AnnotationEditorSessionTest, BeginActivatesSession)
{
  AnnotationEditorSession session;

  ASSERT_TRUE(session.begin(makeCanvas()));
  EXPECT_TRUE(session.isActive());
  EXPECT_EQ(session.source().width, kCanvasWidth);
  EXPECT_EQ(session.source().height, kCanvasHeight);
}

TEST(AnnotationEditorSessionTest, SessionKeepsItsOwnSourceCopy)
{
  AnnotationEditorSession session;
  Image source = makeCanvas();
  ASSERT_TRUE(session.begin(source));

  source.pixels.assign(source.pixels.size(), kBlackPx);

  EXPECT_EQ(pixelAt(session.source(), 0, 0), kWhitePx);
}

TEST(AnnotationEditorSessionTest, BeginWhileActiveIsRejected)
{
  AnnotationEditorSession session;
  ASSERT_TRUE(session.begin(makeCanvas()));

  Image other = makeCanvas();
  other.pixels.assign(other.pixels.size(), kBlackPx);

  EXPECT_FALSE(session.begin(other));
  EXPECT_EQ(pixelAt(session.source(), 0, 0), kWhitePx);
}

TEST(AnnotationEditorSessionTest, BeginClearsPreviousRoundAnnotations)
{
  AnnotationEditorSession session;
  ASSERT_TRUE(session.begin(makeCanvas()));
  ASSERT_TRUE(session.engine().add(makeRedRectangle()));

  AnnotationFinishResult discarded;
  ASSERT_TRUE(session.finishCancelled(discarded));
  ASSERT_TRUE(session.begin(makeCanvas()));

  EXPECT_TRUE(session.engine().document().empty());
  EXPECT_FALSE(session.engine().canUndo());
  EXPECT_FALSE(session.engine().canRedo());
}

TEST(AnnotationEditorSessionTest, ConfirmWithoutAnnotationsReturnsSourceCopy)
{
  AnnotationEditorSession session;
  const Image source = makeCanvas();
  ASSERT_TRUE(session.begin(source));

  AnnotationFinishResult result;
  ASSERT_TRUE(session.finishConfirmed(result));

  EXPECT_FALSE(result.cancelled);
  EXPECT_EQ(result.rendered_image.width, kCanvasWidth);
  EXPECT_EQ(result.rendered_image.height, kCanvasHeight);
  EXPECT_EQ(result.rendered_image.pixels, source.pixels);
}

TEST(AnnotationEditorSessionTest, ConfirmRendersAddedAnnotations)
{
  AnnotationEditorSession session;
  ASSERT_TRUE(session.begin(makeCanvas()));
  ASSERT_TRUE(session.engine().add(makeRedRectangle()));

  AnnotationFinishResult result;
  ASSERT_TRUE(session.finishConfirmed(result));

  EXPECT_FALSE(result.cancelled);
  EXPECT_EQ(pixelAt(result.rendered_image, 10, 8), kRedPx);
  EXPECT_EQ(pixelAt(result.rendered_image, 17, 12), kWhitePx);
}

TEST(AnnotationEditorSessionTest, ConfirmEndsTheRound)
{
  AnnotationEditorSession session;
  ASSERT_TRUE(session.begin(makeCanvas()));

  AnnotationFinishResult result;
  ASSERT_TRUE(session.finishConfirmed(result));

  EXPECT_FALSE(session.isActive());
}

TEST(AnnotationEditorSessionTest, CancelReportsCancelledWithoutImage)
{
  AnnotationEditorSession session;
  ASSERT_TRUE(session.begin(makeCanvas()));
  ASSERT_TRUE(session.engine().add(makeRedRectangle()));

  AnnotationFinishResult result;
  ASSERT_TRUE(session.finishCancelled(result));

  EXPECT_TRUE(result.cancelled);
  EXPECT_TRUE(result.rendered_image.empty());
  EXPECT_FALSE(session.isActive());
}

TEST(AnnotationEditorSessionTest, FinishWithoutBeginIsRejected)
{
  AnnotationEditorSession session;
  AnnotationFinishResult confirmed;
  AnnotationFinishResult cancelled;

  EXPECT_FALSE(session.finishConfirmed(confirmed));
  EXPECT_FALSE(session.finishCancelled(cancelled));
  EXPECT_TRUE(confirmed.cancelled);
  EXPECT_TRUE(confirmed.rendered_image.empty());
}

TEST(AnnotationEditorSessionTest, SecondFinishOnSameRoundIsRejected)
{
  AnnotationEditorSession session;
  ASSERT_TRUE(session.begin(makeCanvas()));

  AnnotationFinishResult first;
  ASSERT_TRUE(session.finishConfirmed(first));

  AnnotationFinishResult second;
  EXPECT_FALSE(session.finishConfirmed(second));
  EXPECT_TRUE(second.rendered_image.empty());
}

}  // namespace qingying
