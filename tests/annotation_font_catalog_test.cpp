#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "qingying/annotate/annotation_font_catalog.hpp"
#include "qingying/annotate/annotation_types.hpp"

namespace qingying {
namespace {

TEST(AnnotationFontCatalogTest,
     NormalizeFiltersEmptyVerticalAliasesAndCaseInsensitiveDuplicates)
{
  const std::vector<std::wstring> input = {
      L"", L"@SimSun", L"arial", L"Arial", L"宋体",
      L"MICROSOFT YAHEI UI"};

  const std::vector<std::wstring> normalized =
      normalizeAnnotationFontFaces(input);

  const std::vector<std::wstring> expected = {
      L"arial", AnnotationTextFontFace, L"宋体"};
  EXPECT_EQ(normalized, expected);
}

TEST(AnnotationFontCatalogTest, NormalizeEmptyInputProvidesDefaultFace)
{
  const std::vector<std::wstring> normalized =
      normalizeAnnotationFontFaces({});

  ASSERT_EQ(normalized.size(), 1u);
  EXPECT_EQ(normalized.front(), AnnotationTextFontFace);
}

TEST(AnnotationFontCatalogTest, CatalogDoesNotEnumerateUntilFirstAccess)
{
  AnnotationFontCatalog catalog;

  EXPECT_FALSE(catalog.loaded());

  const std::vector<std::wstring>& fonts = catalog.fonts();
  EXPECT_TRUE(catalog.loaded());
  EXPECT_FALSE(fonts.empty());
  EXPECT_NE(std::find(fonts.begin(), fonts.end(), AnnotationTextFontFace),
            fonts.end());
}

TEST(AnnotationFontCatalogTest, ClearReleasesLoadedCatalogState)
{
  AnnotationFontCatalog catalog;
  ASSERT_FALSE(catalog.fonts().empty());

  catalog.clear();

  EXPECT_FALSE(catalog.loaded());
}

}  // namespace
}  // namespace qingying
