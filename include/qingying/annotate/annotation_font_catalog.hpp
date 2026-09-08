#pragma once

#include <string>
#include <vector>

namespace qingying {

std::vector<std::wstring> normalizeAnnotationFontFaces(
    const std::vector<std::wstring>& font_faces);

std::vector<std::wstring> enumerateInstalledAnnotationFonts();

class AnnotationFontCatalog
{
 public:
  const std::vector<std::wstring>& fonts();
  const std::vector<std::wstring>& cachedFonts() const;
  bool loaded() const;
  void clear();

 private:
  std::vector<std::wstring> m_fonts;
  bool m_loaded{false};
};

}  // namespace qingying
