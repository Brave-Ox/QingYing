#include "qingying/annotate/annotation_engine.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace qingying {
namespace {

constexpr int kCanvasWidth = 40;
constexpr int kCanvasHeight = 30;
constexpr std::uint32_t kWhitePx = 0xFFFFFFFFu;
constexpr std::uint32_t kRedPx = 0xFFFF0000u;  // a=FF, r=FF, g=00, b=00

// 与渲染器测试保持一致的矩形：左上 (10, 8)，尺寸 16x12。
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

Annotation makeInvalidRectangle()
{
  Annotation annotation = makeRedRectangle();
  annotation.bounds.width = 0.0f;
  return annotation;
}

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

std::uint32_t pixelAt(const Image& image, int x, int y)
{
  return image.pixels.at(static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(image.width) +
                         static_cast<std::size_t>(x));
}

}  // namespace

TEST(AnnotationDocumentTest, CanUndoFollowsItemStack)
{
  AnnotationDocument document;
  EXPECT_FALSE(document.canUndo());
  ASSERT_TRUE(document.add(makeRedRectangle()));
  EXPECT_TRUE(document.canUndo());
  ASSERT_TRUE(document.undo());
  EXPECT_FALSE(document.canUndo());
}

TEST(AnnotationDocumentTest, CanRedoFollowsRedoStack)
{
  AnnotationDocument document;
  EXPECT_FALSE(document.canRedo());
  ASSERT_TRUE(document.add(makeRedRectangle()));
  EXPECT_FALSE(document.canRedo());
  ASSERT_TRUE(document.undo());
  EXPECT_TRUE(document.canRedo());
  ASSERT_TRUE(document.redo());
  EXPECT_FALSE(document.canRedo());
}

TEST(AnnotationEngineTest, NewEngineIsEmptyWithNothingToUndoOrRedo)
{
  const AnnotationEngine engine;
  EXPECT_TRUE(engine.document().empty());
  EXPECT_FALSE(engine.canUndo());
  EXPECT_FALSE(engine.canRedo());
}

TEST(AnnotationEngineTest, AddStoresAnnotationInDocument)
{
  AnnotationEngine engine;
  ASSERT_TRUE(engine.add(makeRedRectangle()));
  EXPECT_EQ(engine.document().count(), 1u);
  EXPECT_TRUE(engine.canUndo());
}

TEST(AnnotationEngineTest, AddRejectsInvalidAnnotation)
{
  AnnotationEngine engine;
  EXPECT_FALSE(engine.add(makeInvalidRectangle()));
  EXPECT_TRUE(engine.document().empty());
  EXPECT_FALSE(engine.canUndo());
}

TEST(AnnotationEngineTest, UndoAndRedoDelegateToDocument)
{
  AnnotationEngine engine;
  ASSERT_TRUE(engine.add(makeRedRectangle()));

  ASSERT_TRUE(engine.undo());
  EXPECT_TRUE(engine.document().empty());
  EXPECT_TRUE(engine.canRedo());

  ASSERT_TRUE(engine.redo());
  EXPECT_EQ(engine.document().count(), 1u);
  EXPECT_FALSE(engine.canRedo());
}

TEST(AnnotationEngineTest, UndoOnEmptyEngineReturnsFalse)
{
  AnnotationEngine engine;
  EXPECT_FALSE(engine.undo());
  EXPECT_FALSE(engine.redo());
}

TEST(AnnotationEngineTest, ClearDropsAnnotationsAndBothStacks)
{
  AnnotationEngine engine;
  ASSERT_TRUE(engine.add(makeRedRectangle()));
  ASSERT_TRUE(engine.add(makeRedRectangle()));
  ASSERT_TRUE(engine.undo());

  engine.clear();

  EXPECT_TRUE(engine.document().empty());
  EXPECT_FALSE(engine.canUndo());
  EXPECT_FALSE(engine.canRedo());
}

TEST(AnnotationEngineTest, RenderRejectsEmptySource)
{
  const AnnotationEngine engine;
  const Image source;
  Image out = makeCanvas();

  EXPECT_FALSE(engine.render(source, out));
  EXPECT_TRUE(out.empty());
}

TEST(AnnotationEngineTest, RenderWithoutAnnotationsCopiesSource)
{
  const AnnotationEngine engine;
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(engine.render(source, out));
  EXPECT_EQ(out.width, kCanvasWidth);
  EXPECT_EQ(out.height, kCanvasHeight);
  EXPECT_EQ(out.pixels, source.pixels);
}

TEST(AnnotationEngineTest, RenderDrawsAddedRectangle)
{
  AnnotationEngine engine;
  ASSERT_TRUE(engine.add(makeRedRectangle()));
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(engine.render(source, out));
  EXPECT_EQ(pixelAt(out, 10, 8), kRedPx);     // 描边
  EXPECT_EQ(pixelAt(out, 17, 12), kWhitePx);  // 内部不填充
  EXPECT_EQ(source.pixels, makeCanvas().pixels);  // 源图未被改写
}

TEST(AnnotationEngineTest, UndoneAnnotationIsNotRendered)
{
  AnnotationEngine engine;
  ASSERT_TRUE(engine.add(makeRedRectangle()));
  ASSERT_TRUE(engine.undo());
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(engine.render(source, out));
  EXPECT_EQ(pixelAt(out, 10, 8), kWhitePx);
}

TEST(AnnotationEngineTest, ReplaceAtDelegatesToDocument)
{
  AnnotationEngine engine;
  Annotation text;
  text.type = AnnotationType::Text;
  text.start = PointF{3.0f, 4.0f};
  text.text = L"a";
  ASSERT_TRUE(engine.add(text));

  Annotation moved = text;
  moved.start = PointF{10.0f, 12.0f};
  moved.text = L"b";
  ASSERT_TRUE(engine.replaceAt(0, moved));
  ASSERT_EQ(engine.document().count(), 1u);
  EXPECT_EQ(engine.document().items().at(0).text, L"b");
  EXPECT_FLOAT_EQ(engine.document().items().at(0).start.x, 10.0f);
}

TEST(AnnotationEngineTest, RemoveAtDelegatesToDocument)
{
  AnnotationEngine engine;
  Annotation text;
  text.type = AnnotationType::Text;
  text.start = PointF{3.0f, 4.0f};
  text.text = L"a";
  ASSERT_TRUE(engine.add(text));
  ASSERT_TRUE(engine.removeAt(0));
  EXPECT_TRUE(engine.document().empty());
  EXPECT_FALSE(engine.removeAt(0));
}

}  // namespace qingying
