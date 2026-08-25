#include "qingying/annotate/annotation_document.hpp"

#include <gtest/gtest.h>

namespace qingying {
namespace {

constexpr float kRectX = 10.0f;
constexpr float kRectY = 20.0f;
constexpr float kRectWidth = 30.0f;
constexpr float kRectHeight = 40.0f;
constexpr float kOtherWidth = 50.0f;
constexpr float kOtherHeight = 60.0f;

Annotation makeRectangle()
{
  Annotation annotation;
  annotation.type = AnnotationType::Rectangle;
  annotation.bounds.x = kRectX;
  annotation.bounds.y = kRectY;
  annotation.bounds.width = kRectWidth;
  annotation.bounds.height = kRectHeight;
  return annotation;
}

Annotation makeOtherRectangle()
{
  Annotation annotation = makeRectangle();
  annotation.bounds.width = kOtherWidth;
  annotation.bounds.height = kOtherHeight;
  return annotation;
}

}  // namespace

TEST(AnnotationDocumentTest, EmptyByDefault)
{
  AnnotationDocument document;
  EXPECT_TRUE(document.empty());
  EXPECT_EQ(document.count(), 0u);
  EXPECT_TRUE(document.items().empty());
}

TEST(AnnotationDocumentTest, AddValidRectangleIncreasesCount)
{
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRectangle()));
  EXPECT_FALSE(document.empty());
  EXPECT_EQ(document.count(), 1u);
}

TEST(AnnotationDocumentTest, ItemsExposesAddedRectangle)
{
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRectangle()));
  ASSERT_EQ(document.items().size(), 1u);
  const Annotation& item = document.items().at(0);
  EXPECT_EQ(item.type, AnnotationType::Rectangle);
  EXPECT_EQ(item.bounds.x, kRectX);
  EXPECT_EQ(item.bounds.y, kRectY);
  EXPECT_EQ(item.bounds.width, kRectWidth);
  EXPECT_EQ(item.bounds.height, kRectHeight);
}

TEST(AnnotationDocumentTest, UndoRemovesLastAnnotation)
{
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRectangle()));
  EXPECT_TRUE(document.undo());
  EXPECT_TRUE(document.empty());
  EXPECT_EQ(document.count(), 0u);
}

TEST(AnnotationDocumentTest, UndoOnEmptyReturnsFalse)
{
  AnnotationDocument document;
  EXPECT_FALSE(document.undo());
}

TEST(AnnotationDocumentTest, ConsecutiveUndoStopsAtEmpty)
{
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRectangle()));
  ASSERT_TRUE(document.add(makeRectangle()));
  EXPECT_TRUE(document.undo());
  EXPECT_EQ(document.count(), 1u);
  EXPECT_TRUE(document.undo());
  EXPECT_TRUE(document.empty());
  EXPECT_FALSE(document.undo());
}

TEST(AnnotationDocumentTest, RedoRestoresUndoneAnnotation)
{
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRectangle()));
  ASSERT_TRUE(document.undo());
  ASSERT_TRUE(document.redo());
  ASSERT_EQ(document.count(), 1u);
  EXPECT_EQ(document.items().at(0).bounds.width, kRectWidth);
  EXPECT_EQ(document.items().at(0).bounds.height, kRectHeight);
}

TEST(AnnotationDocumentTest, RedoWithoutUndoReturnsFalse)
{
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRectangle()));
  EXPECT_FALSE(document.redo());
}

TEST(AnnotationDocumentTest, AddAfterUndoClearsRedoStack)
{
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRectangle()));
  ASSERT_TRUE(document.undo());
  ASSERT_TRUE(document.add(makeOtherRectangle()));
  EXPECT_FALSE(document.redo());
  ASSERT_EQ(document.count(), 1u);
  EXPECT_EQ(document.items().at(0).bounds.width, kOtherWidth);
  EXPECT_EQ(document.items().at(0).bounds.height, kOtherHeight);
}

TEST(AnnotationDocumentTest, ClearDropsItemsAndRedoStack)
{
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRectangle()));
  ASSERT_TRUE(document.add(makeOtherRectangle()));
  ASSERT_TRUE(document.undo());
  document.clear();
  EXPECT_TRUE(document.empty());
  EXPECT_EQ(document.count(), 0u);
  EXPECT_FALSE(document.undo());
  EXPECT_FALSE(document.redo());
}

TEST(AnnotationDocumentTest, RejectEmptyRectangle)
{
  AnnotationDocument document;
  Annotation annotation = makeRectangle();
  annotation.bounds.width = 0.0f;
  EXPECT_FALSE(document.add(annotation));
  EXPECT_TRUE(document.empty());
}

TEST(AnnotationDocumentTest, RejectPenWithFewerThanTwoPoints)
{
  AnnotationDocument document;
  Annotation annotation;
  annotation.type = AnnotationType::Pen;
  annotation.points.push_back(PointF{1.0f, 2.0f});
  EXPECT_FALSE(document.add(annotation));
  EXPECT_EQ(document.count(), 0u);
}

TEST(AnnotationDocumentTest, RejectEmptyText)
{
  AnnotationDocument document;
  Annotation annotation;
  annotation.type = AnnotationType::Text;
  annotation.start = PointF{8.0f, 16.0f};
  annotation.text.clear();
  EXPECT_FALSE(document.add(annotation));
  EXPECT_EQ(document.count(), 0u);
}

TEST(AnnotationDocumentTest, RejectedAddDoesNotClearRedoStack)
{
  AnnotationDocument document;
  ASSERT_TRUE(document.add(makeRectangle()));
  ASSERT_TRUE(document.undo());

  Annotation invalid = makeRectangle();
  invalid.bounds.height = 0.0f;
  EXPECT_FALSE(document.add(invalid));

  ASSERT_TRUE(document.redo());
  EXPECT_EQ(document.items().at(0).bounds.width, kRectWidth);
}

}  // namespace qingying
