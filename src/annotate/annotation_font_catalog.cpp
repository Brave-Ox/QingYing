#include <algorithm>
#include <memory>
#include <new>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "qingying/annotate/annotation_font_catalog.hpp"
#include "qingying/annotate/annotation_types.hpp"

namespace qingying {
namespace {

struct ScreenDcReleaser
{
  void operator()(HDC hdc) const
  {
    if (hdc != nullptr)
    {
      // A deleter cannot propagate ReleaseDC failure and must not retry it.
      (void)ReleaseDC(nullptr, hdc);
    }
  }
};

using ScopedScreenDc = std::unique_ptr<HDC__, ScreenDcReleaser>;

bool fontFaceEquals(const std::wstring& lhs, const std::wstring& rhs)
{
  return CompareStringOrdinal(lhs.c_str(), -1, rhs.c_str(), -1, TRUE) ==
         CSTR_EQUAL;
}

bool fontFaceLess(const std::wstring& lhs, const std::wstring& rhs)
{
  return CompareStringOrdinal(lhs.c_str(), -1, rhs.c_str(), -1, TRUE) ==
         CSTR_LESS_THAN;
}

struct FontEnumerationContext
{
  std::vector<std::wstring>* font_faces{nullptr};
  bool allocation_failed{false};
};

int CALLBACK collectFontFace(const LOGFONTW* log_font,
                             const TEXTMETRICW*, DWORD, LPARAM parameter)
{
  FontEnumerationContext* context =
      reinterpret_cast<FontEnumerationContext*>(parameter);
  if (context == nullptr || context->font_faces == nullptr ||
      log_font == nullptr)
  {
    return 0;
  }

  try
  {
    context->font_faces->emplace_back(log_font->lfFaceName);
  }
  catch (const std::bad_alloc&)
  {
    context->allocation_failed = true;
    return 0;
  }
  return 1;
}

}  // namespace

std::vector<std::wstring> normalizeAnnotationFontFaces(
    const std::vector<std::wstring>& font_faces)
{
  std::vector<std::wstring> normalized;
  normalized.reserve(font_faces.size() + 1u);
  for (const std::wstring& font_face : font_faces)
  {
    if (font_face.empty() || font_face.front() == L'@')
    {
      continue;
    }
    if (fontFaceEquals(font_face, AnnotationTextFontFace))
    {
      normalized.emplace_back(AnnotationTextFontFace);
      continue;
    }
    normalized.push_back(font_face);
  }

  normalized.emplace_back(AnnotationTextFontFace);
  std::stable_sort(normalized.begin(), normalized.end(), fontFaceLess);
  normalized.erase(
      std::unique(normalized.begin(), normalized.end(), fontFaceEquals),
      normalized.end());
  return normalized;
}

std::vector<std::wstring> enumerateInstalledAnnotationFonts()
{
  const ScopedScreenDc screen_dc(GetDC(nullptr));
  if (!screen_dc)
  {
    return normalizeAnnotationFontFaces({});
  }

  LOGFONTW query{};
  query.lfCharSet = DEFAULT_CHARSET;
  std::vector<std::wstring> font_faces;
  FontEnumerationContext context{&font_faces, false};
  const int enumeration_result = EnumFontFamiliesExW(
      screen_dc.get(), &query, collectFontFace,
      reinterpret_cast<LPARAM>(&context), 0);
  if (enumeration_result == 0 || context.allocation_failed)
  {
    return normalizeAnnotationFontFaces({});
  }
  return normalizeAnnotationFontFaces(font_faces);
}

const std::vector<std::wstring>& AnnotationFontCatalog::fonts()
{
  if (!m_loaded)
  {
    m_fonts = enumerateInstalledAnnotationFonts();
    m_loaded = true;
  }
  return m_fonts;
}

const std::vector<std::wstring>& AnnotationFontCatalog::cachedFonts() const
{
  return m_fonts;
}

bool AnnotationFontCatalog::loaded() const
{
  return m_loaded;
}

void AnnotationFontCatalog::clear()
{
  std::vector<std::wstring>().swap(m_fonts);
  m_loaded = false;
}

}  // namespace qingying
