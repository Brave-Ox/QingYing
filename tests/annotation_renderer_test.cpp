#include "qingying/annotate/annotation_renderer.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace qingying {
namespace {

constexpr int kCanvasWidth = 40;
constexpr int kCanvasHeight = 30;
constexpr std::uint32_t kWhitePx = 0xFFFFFFFFu;

// 测试矩形：左上 (10, 8)，尺寸 16x12 → 内缘边界 x∈[10,25]、y∈[8,19]。
constexpr float kRectX = 10.0f;
constexpr float kRectY = 8.0f;
constexpr float kRectWidth = 16.0f;
constexpr float kRectHeight = 12.0f;
constexpr float kStrokeWidth = 2.0f;

constexpr std::uint32_t makeBgra(std::uint8_t b, std::uint8_t g,
                                 std::uint8_t r, std::uint8_t a)
{
  return (static_cast<std::uint32_t>(a) << 24) |
         (static_cast<std::uint32_t>(r) << 16) |
         (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
}

constexpr std::uint32_t kRedPx = makeBgra(0, 0, 255, 255);

Annotation makeRedRectangle()
{
  Annotation annotation;
  annotation.type = AnnotationType::Rectangle;
  annotation.bounds.x = kRectX;
  annotation.bounds.y = kRectY;
  annotation.bounds.width = kRectWidth;
  annotation.bounds.height = kRectHeight;
  annotation.style.color = ColorBgra{0, 0, 255, 255};
  annotation.style.stroke_width = kStrokeWidth;
  return annotation;
}

// 正圆：左上 (10, 5)，20x20 → 像素边界 [10,29]×[5,24]，中心 (19.5, 14.5)，半径 9.5。
constexpr float kEllipseX = 10.0f;
constexpr float kEllipseY = 5.0f;
constexpr float kEllipseSize = 20.0f;

Annotation makeRedEllipse()
{
  Annotation annotation;
  annotation.type = AnnotationType::Ellipse;
  annotation.bounds.x = kEllipseX;
  annotation.bounds.y = kEllipseY;
  annotation.bounds.width = kEllipseSize;
  annotation.bounds.height = kEllipseSize;
  annotation.style.color = ColorBgra{0, 0, 255, 255};
  annotation.style.stroke_width = kStrokeWidth;
  return annotation;
}

// 水平箭头：(5, 15) → (30, 15)，线宽 1 便于逐像素断言。
constexpr float kArrowStartX = 5.0f;
constexpr float kArrowEndX = 30.0f;
constexpr float kArrowY = 15.0f;
constexpr int kArrowRowY = 15;

Annotation makeRedArrow()
{
  Annotation annotation;
  annotation.type = AnnotationType::Arrow;
  annotation.start = PointF{kArrowStartX, kArrowY};
  annotation.end = PointF{kArrowEndX, kArrowY};
  annotation.style.color = ColorBgra{0, 0, 255, 255};
  annotation.style.stroke_width = 1.0f;
  return annotation;
}

Annotation makeRedPen()
{
  Annotation annotation;
  annotation.type = AnnotationType::Pen;
  annotation.points.push_back(PointF{5.0f, 5.0f});
  annotation.points.push_back(PointF{25.0f, 5.0f});
  annotation.points.push_back(PointF{25.0f, 25.0f});
  annotation.style.color = ColorBgra{0, 0, 255, 255};
  annotation.style.stroke_width = 1.0f;
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

// 统计以 (cx, cy) 为中心的方框内、不在 axis_y 这一行上的着色像素数量。
// 用于区分「只有主干线」和「主干线 + 箭头头部」。
int countColoredOffAxis(const Image& image, int cx, int cy, int radius,
                        int axis_y, std::uint32_t color)
{
  int found = 0;
  for (int y = cy - radius; y <= cy + radius; ++y)
  {
    for (int x = cx - radius; x <= cx + radius; ++x)
    {
      if (x < 0 || y < 0 || x >= image.width || y >= image.height)
      {
        continue;
      }
      if (y != axis_y && pixelAt(image, x, y) == color)
      {
        ++found;
      }
    }
  }
  return found;
}

}  // namespace

TEST(AnnotationRendererTest, EmptySourceReturnsFalse)
{
  const AnnotationRenderer renderer;
  const AnnotationDocument document;
  const Image source;
  Image out = makeCanvas();

  EXPECT_FALSE(renderer.rasterize(source, document, out));
  EXPECT_TRUE(out.empty());
}

TEST(AnnotationRendererTest, EmptyDocumentCopiesSource)
{
  const AnnotationRenderer renderer;
  const AnnotationDocument document;
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(renderer.rasterize(source, document, out));
  EXPECT_EQ(out.width, kCanvasWidth);
  EXPECT_EQ(out.height, kCanvasHeight);
  ASSERT_EQ(out.pixels.size(), source.pixels.size());
  EXPECT_EQ(out.pixels, source.pixels);
}

TEST(AnnotationRendererTest, RectangleStrokeIsDrawnOnBorder)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRedRectangle()));
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(renderer.rasterize(source, document, out));
  EXPECT_EQ(pixelAt(out, 10, 8), kRedPx);   // 左上角
  EXPECT_EQ(pixelAt(out, 25, 19), kRedPx);  // 右下角
  EXPECT_EQ(pixelAt(out, 17, 8), kRedPx);   // 上边最外一行
  EXPECT_EQ(pixelAt(out, 17, 9), kRedPx);   // 上边内侧一行（线宽 2）
  EXPECT_EQ(pixelAt(out, 24, 12), kRedPx);  // 右边内侧一列
}

TEST(AnnotationRendererTest, RectangleInteriorIsNotFilled)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRedRectangle()));
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(renderer.rasterize(source, document, out));
  EXPECT_EQ(pixelAt(out, 17, 12), kWhitePx);  // 正中间
  EXPECT_EQ(pixelAt(out, 12, 10), kWhitePx);  // 距上/左各 2px，已在描边之内
  EXPECT_EQ(pixelAt(out, 23, 12), kWhitePx);  // 距右 2px
}

TEST(AnnotationRendererTest, PixelsOutsideRectangleAreUnchanged)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRedRectangle()));
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(renderer.rasterize(source, document, out));
  EXPECT_EQ(pixelAt(out, 0, 0), kWhitePx);
  EXPECT_EQ(pixelAt(out, 17, 7), kWhitePx);   // 紧贴上边界之外
  EXPECT_EQ(pixelAt(out, 26, 12), kWhitePx);  // 紧贴右边界之外
  EXPECT_EQ(pixelAt(out, 39, 29), kWhitePx);
}

TEST(AnnotationRendererTest, SourceImageIsNotModified)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRedRectangle()));
  const Image source = makeCanvas();
  const std::vector<std::uint32_t> before = source.pixels;
  Image out;

  ASSERT_TRUE(renderer.rasterize(source, document, out));
  EXPECT_EQ(source.pixels, before);
}

TEST(AnnotationRendererTest, RectangleClippedAtCanvasEdge)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  Annotation annotation = makeRedRectangle();
  annotation.bounds.x = 35.0f;  // 右侧超出画布（宽 40）
  annotation.bounds.y = 5.0f;
  annotation.bounds.width = 20.0f;
  annotation.bounds.height = 10.0f;
  ASSERT_TRUE(document.add(annotation));
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(renderer.rasterize(source, document, out));
  ASSERT_EQ(out.pixels.size(), source.pixels.size());
  EXPECT_EQ(pixelAt(out, 35, 5), kRedPx);     // 可见的左上角
  EXPECT_EQ(pixelAt(out, 39, 10), kWhitePx);  // 右边界被裁掉，此处仍是内部
}

TEST(AnnotationRendererTest, EllipseStrokeIsDrawnOnPerimeter)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRedEllipse()));
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(renderer.rasterize(source, document, out));
  EXPECT_EQ(pixelAt(out, 10, 14), kRedPx);  // 左端
  EXPECT_EQ(pixelAt(out, 29, 14), kRedPx);  // 右端
  EXPECT_EQ(pixelAt(out, 19, 5), kRedPx);   // 上端
  EXPECT_EQ(pixelAt(out, 19, 24), kRedPx);  // 下端
}

TEST(AnnotationRendererTest, EllipseInteriorIsNotFilled)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRedEllipse()));
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(renderer.rasterize(source, document, out));
  EXPECT_EQ(pixelAt(out, 19, 14), kWhitePx);  // 圆心附近
  EXPECT_EQ(pixelAt(out, 19, 10), kWhitePx);  // 半径内、描边环之内
}

TEST(AnnotationRendererTest, PixelsOutsideEllipseAreUnchanged)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRedEllipse()));
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(renderer.rasterize(source, document, out));
  EXPECT_EQ(pixelAt(out, 0, 0), kWhitePx);
  EXPECT_EQ(pixelAt(out, 19, 4), kWhitePx);   // 紧贴上端之外
  EXPECT_EQ(pixelAt(out, 30, 14), kWhitePx);  // 紧贴右端之外
}

TEST(AnnotationRendererTest, EllipseClippedAtCanvasEdge)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  Annotation annotation = makeRedEllipse();
  annotation.bounds.x = 30.0f;  // 右侧超出画布（宽 40）
  annotation.bounds.y = 5.0f;
  annotation.bounds.width = 20.0f;
  annotation.bounds.height = 20.0f;
  ASSERT_TRUE(document.add(annotation));
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(renderer.rasterize(source, document, out));
  EXPECT_EQ(pixelAt(out, 30, 14), kRedPx);  // 可见左端
  EXPECT_EQ(out.pixels.size(), source.pixels.size());
}

TEST(AnnotationRendererTest, ArrowLineConnectsEndpoints)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRedArrow()));
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(renderer.rasterize(source, document, out));
  EXPECT_EQ(pixelAt(out, 5, kArrowRowY), kRedPx);   // 起点
  EXPECT_EQ(pixelAt(out, 17, kArrowRowY), kRedPx);  // 中段
  EXPECT_EQ(pixelAt(out, 30, kArrowRowY), kRedPx);  // 终点
  EXPECT_EQ(pixelAt(out, 4, kArrowRowY), kWhitePx);   // 起点之前不画
  EXPECT_EQ(pixelAt(out, 31, kArrowRowY), kWhitePx);  // 终点之后不画
}

TEST(AnnotationRendererTest, DiagonalArrowLineHasNoGaps)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  Annotation annotation = makeRedArrow();
  annotation.start = PointF{2.0f, 2.0f};
  annotation.end = PointF{22.0f, 12.0f};
  ASSERT_TRUE(document.add(annotation));
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(renderer.rasterize(source, document, out));
  // 斜线每一列都应至少落一个像素，否则线是断的。
  for (int x = 2; x <= 22; ++x)
  {
    bool column_has_pixel = false;
    for (int y = 0; y < kCanvasHeight; ++y)
    {
      if (pixelAt(out, x, y) == kRedPx)
      {
        column_has_pixel = true;
        break;
      }
    }
    EXPECT_TRUE(column_has_pixel) << "斜线在 x=" << x << " 处断开";
  }
}

TEST(AnnotationRendererTest, ArrowHeadIsDrawnAtEndOnly)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRedArrow()));
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(renderer.rasterize(source, document, out));
  const int near_end =
      countColoredOffAxis(out, 30, kArrowRowY, 6, kArrowRowY, kRedPx);
  const int near_start =
      countColoredOffAxis(out, 5, kArrowRowY, 6, kArrowRowY, kRedPx);
  EXPECT_GT(near_end, 0);    // 终点有箭头头部
  EXPECT_EQ(near_start, 0);  // 起点只有主干线
}

TEST(AnnotationRendererTest, ArrowStrokeWidthWidensLine)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  Annotation annotation = makeRedArrow();
  annotation.style.stroke_width = 3.0f;
  ASSERT_TRUE(document.add(annotation));
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(renderer.rasterize(source, document, out));
  EXPECT_EQ(pixelAt(out, 17, kArrowRowY - 1), kRedPx);
  EXPECT_EQ(pixelAt(out, 17, kArrowRowY), kRedPx);
  EXPECT_EQ(pixelAt(out, 17, kArrowRowY + 1), kRedPx);
  EXPECT_EQ(pixelAt(out, 17, kArrowRowY + 2), kWhitePx);
}

TEST(AnnotationRendererTest, ArrowClippedAtCanvasEdge)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  Annotation annotation = makeRedArrow();
  annotation.start = PointF{-10.0f, kArrowY};  // 两端都在画布外
  annotation.end = PointF{50.0f, kArrowY};
  ASSERT_TRUE(document.add(annotation));
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(renderer.rasterize(source, document, out));
  ASSERT_EQ(out.pixels.size(), source.pixels.size());
  EXPECT_EQ(pixelAt(out, 0, kArrowRowY), kRedPx);
  EXPECT_EQ(pixelAt(out, 39, kArrowRowY), kRedPx);
}

TEST(AnnotationRendererTest, PenConnectsConsecutivePoints)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRedPen()));
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(renderer.rasterize(source, document, out));
  EXPECT_EQ(pixelAt(out, 5, 5), kRedPx);    // 第一个点
  EXPECT_EQ(pixelAt(out, 15, 5), kRedPx);   // 第一段中点
  EXPECT_EQ(pixelAt(out, 25, 5), kRedPx);   // 拐点
  EXPECT_EQ(pixelAt(out, 25, 15), kRedPx);  // 第二段中点
  EXPECT_EQ(pixelAt(out, 25, 25), kRedPx);  // 最后一个点
}

TEST(AnnotationRendererTest, PenDoesNotCloseThePath)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRedPen()));
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(renderer.rasterize(source, document, out));
  // (15, 15) 只落在「末点 → 首点」这条闭合线上，不应被画出。
  EXPECT_EQ(pixelAt(out, 15, 15), kWhitePx);
}

TEST(AnnotationRendererTest, PreviewAnnotationIsDrawnWithoutEnteringDocument)
{
  const AnnotationRenderer renderer;
  const AnnotationDocument empty;
  const Annotation preview = makeRedRectangle();
  const Image source = makeCanvas();
  Image out;

  ASSERT_TRUE(renderer.rasterize(source, empty, &preview, out));
  EXPECT_EQ(pixelAt(out, 10, 8), kRedPx);
  EXPECT_TRUE(empty.empty());  // 预览不得污染文档
}

TEST(AnnotationRendererTest, TextDrawsNonEmptyStringChangingPixels)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  Annotation text;
  text.type = AnnotationType::Text;
  text.start = PointF{4.0f, 4.0f};
  text.text = L"Hi";
  text.style.color = ColorBgra{0, 0, 255, 255};
  text.style.font_size = DefaultFontSize;
  ASSERT_TRUE(document.add(text));

  const Image source = makeCanvas();
  Image out;
  ASSERT_TRUE(renderer.rasterize(source, document, out));
  EXPECT_NE(out.pixels, source.pixels);
  EXPECT_EQ(source.pixels.size(), out.pixels.size());
}

TEST(AnnotationRendererTest, TextLeavesSourceImageUnmodified)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  Annotation text;
  text.type = AnnotationType::Text;
  text.start = PointF{4.0f, 4.0f};
  text.text = L"Hi";
  text.style.color = ColorBgra{0, 0, 255, 255};
  ASSERT_TRUE(document.add(text));

  const Image source = makeCanvas();
  const std::vector<std::uint32_t> before = source.pixels;
  Image out;
  ASSERT_TRUE(renderer.rasterize(source, document, out));
  EXPECT_EQ(source.pixels, before);
}

TEST(AnnotationRendererTest, TextUsesRequestedFontSizeChangingMorePixels)
{
  const AnnotationRenderer renderer;
  AnnotationDocument small_doc;
  AnnotationDocument large_doc;

  Annotation small_text;
  small_text.type = AnnotationType::Text;
  small_text.start = PointF{2.0f, 2.0f};
  small_text.text = L"A";
  small_text.style.color = ColorBgra{0, 0, 255, 255};
  small_text.style.font_size = 12;
  ASSERT_TRUE(small_doc.add(small_text));

  Annotation large_text = small_text;
  large_text.style.font_size = 32;
  ASSERT_TRUE(large_doc.add(large_text));

  const Image source = makeCanvas();
  Image small_out;
  Image large_out;
  ASSERT_TRUE(renderer.rasterize(source, small_doc, small_out));
  ASSERT_TRUE(renderer.rasterize(source, large_doc, large_out));
  EXPECT_NE(small_out.pixels, source.pixels);
  EXPECT_NE(large_out.pixels, source.pixels);
  EXPECT_NE(small_out.pixels, large_out.pixels);
}

TEST(AnnotationRendererTest, MosaicBrushPixelatesTouchedBlocks)
{
  // 24x24 画布、块大小 12：左上块像素各不相同，画笔穿过后整块应变成均值色。
  constexpr int kSize = 24;
  constexpr int kBlock = 12;
  Image source;
  source.width = kSize;
  source.height = kSize;
  source.pixels.resize(static_cast<std::size_t>(kSize) *
                        static_cast<std::size_t>(kSize));
  for (int y = 0; y < kSize; ++y)
  {
    for (int x = 0; x < kSize; ++x)
    {
      const std::size_t index = static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(kSize) +
                                static_cast<std::size_t>(x);
      source.pixels.at(index) =
          makeBgra(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y),
                   40, 255);
    }
  }

  Annotation mosaic;
  mosaic.type = AnnotationType::Mosaic;
  mosaic.mosaic_block_size = kBlock;
  mosaic.style.stroke_width = 4.0f;
  mosaic.points.push_back(PointF{2.0f, 6.0f});
  mosaic.points.push_back(PointF{10.0f, 6.0f});

  AnnotationDocument document;
  ASSERT_TRUE(document.add(mosaic));

  const AnnotationRenderer renderer;
  Image out;
  ASSERT_TRUE(renderer.rasterize(source, document, out));
  EXPECT_NE(out.pixels, source.pixels);

  // 左上块 [0,12)×[0,12) 被笔刷碰到：整块应为源图该块均值，且块内一致。
  std::uint64_t sum_b = 0;
  std::uint64_t sum_g = 0;
  std::uint64_t sum_r = 0;
  std::uint64_t sum_a = 0;
  std::size_t count = 0;
  for (int y = 0; y < kBlock; ++y)
  {
    for (int x = 0; x < kBlock; ++x)
    {
      const std::size_t index = static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(kSize) +
                                static_cast<std::size_t>(x);
      const std::uint32_t px = source.pixels.at(index);
      sum_b += px & 0xFFu;
      sum_g += (px >> 8) & 0xFFu;
      sum_r += (px >> 16) & 0xFFu;
      sum_a += (px >> 24) & 0xFFu;
      ++count;
    }
  }
  const std::uint32_t expected = makeBgra(
      static_cast<std::uint8_t>(sum_b / count),
      static_cast<std::uint8_t>(sum_g / count),
      static_cast<std::uint8_t>(sum_r / count),
      static_cast<std::uint8_t>(sum_a / count));

  for (int y = 0; y < kBlock; ++y)
  {
    for (int x = 0; x < kBlock; ++x)
    {
      const std::size_t index = static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(kSize) +
                                static_cast<std::size_t>(x);
      EXPECT_EQ(out.pixels.at(index), expected) << "x=" << x << " y=" << y;
    }
  }

  // 右下块未被笔刷碰到，应保持原像素。
  const std::size_t far_index =
      static_cast<std::size_t>(20) * static_cast<std::size_t>(kSize) + 20u;
  EXPECT_EQ(out.pixels.at(far_index), source.pixels.at(far_index));
}

TEST(AnnotationRendererTest, MosaicWithTooFewPointsDoesNotModifyPixels)
{
  const AnnotationRenderer renderer;
  AnnotationDocument document;
  // 绕过 Document：直接测渲染器对非法点数的容忍。
  Annotation mosaic;
  mosaic.type = AnnotationType::Mosaic;
  mosaic.points.push_back(PointF{5.0f, 5.0f});
  mosaic.mosaic_block_size = DefaultMosaicBlockSize;

  const Image source = makeCanvas();
  Image out = source;
  // 经 preview 路径传入单点马赛克，应不改像素。
  ASSERT_TRUE(renderer.rasterize(source, document, &mosaic, out));
  EXPECT_EQ(out.pixels, source.pixels);
}

}  // namespace qingying
