#include "qingying/overlay/overlay_renderer.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>

#include "qingying/overlay/mask_renderer.hpp"
#include "qingying/overlay/selection_handles.hpp"

namespace qingying {

namespace {

constexpr std::uint32_t kHandlePixel = 0xFFFFFFFFu;  // 手柄：不透明白
constexpr std::uint32_t kHoverPixel = 0xFF00B4FFu;   // 窗口吸附悬停高亮：亮蓝
constexpr std::uint32_t kHoverGlowPixel = 0x70204C70u;
// 悬停区域：几乎透明，合成后露出冻结背景的原始颜色。
constexpr std::uint32_t kHoverRevealPixel = 0x01000000u;
constexpr std::uint32_t kHoverLabelPanelPixel = 0xE01A202Au;
constexpr std::uint32_t kHoverLabelTextPixel = 0xFFFFFFFFu;
constexpr int kHoverThickness = 2;
constexpr int kHoverLabelGap = 4;
constexpr int kHoverLabelPadding = 4;
constexpr int kHoverLabelGlyphWidth = 3;
constexpr int kHoverLabelGlyphHeight = 5;
constexpr int kHoverLabelGlyphScale = 2;
constexpr int kHoverLabelGlyphGap = 2;

// CreateCompatibleDC RAII：DeleteDC。
struct CompatibleDcDeleter {
  void operator()(HDC hdc) const noexcept {
    if (hdc != nullptr) {
      DeleteDC(hdc);
    }
  }
};

// CreateDIBSection 的 HBITMAP RAII：DeleteObject。
struct DibDeleter {
  void operator()(HBITMAP bitmap) const noexcept {
    if (bitmap != nullptr) {
      DeleteObject(bitmap);
    }
  }
};

// GetDC 的 HDC RAII：ReleaseDC。
struct ScreenHdcDeleter {
  void operator()(HDC hdc) const noexcept {
    if (hdc != nullptr) {
      ReleaseDC(nullptr, hdc);
    }
  }
};

// 像素缓冲写点（越界跳过）。
void setOverlayPixel(std::vector<std::uint32_t>& pixels, int width, int height,
                     int x, int y, std::uint32_t color) {
  if (x < 0 || y < 0 || x >= width || y >= height) {
    return;
  }
  pixels.at(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
            static_cast<std::size_t>(x)) = color;
}

// 绘制窗口吸附悬停高亮边框（矩形轮廓）。
void drawHoverOutline(std::vector<std::uint32_t>& pixels, int width, int height,
                      const OverlayClientRect& rect, std::uint32_t color,
                      int thickness) {
  if (rect.width <= 0 || rect.height <= 0) {
    return;
  }
  const int right = rect.x + rect.width - 1;
  const int bottom = rect.y + rect.height - 1;
  for (int t = 0; t < thickness; ++t) {
    for (int i = rect.x; i <= right; ++i) {
      setOverlayPixel(pixels, width, height, i, rect.y + t, color);
      setOverlayPixel(pixels, width, height, i, bottom - t, color);
    }
    for (int j = rect.y; j <= bottom; ++j) {
      setOverlayPixel(pixels, width, height, rect.x + t, j, color);
      setOverlayPixel(pixels, width, height, right - t, j, color);
    }
  }
}

void drawHoverGlow(std::vector<std::uint32_t>& pixels, int width, int height,
                   const OverlayClientRect& rect)
{
  if (rect.width <= 0 || rect.height <= 0) {
    return;
  }
  const OverlayClientRect glow_rect{rect.x - 1, rect.y - 1, rect.width + 2,
                                    rect.height + 2};
  drawHoverOutline(pixels, width, height, glow_rect, kHoverGlowPixel, 1);
}

void revealHoverBackground(std::vector<std::uint32_t>& pixels, int width,
                           int height, const OverlayClientRect& rect)
{
  const int left = (std::max)(0, rect.x);
  const int top = (std::max)(0, rect.y);
  const int right = (std::min)(width, rect.x + rect.width);
  const int bottom = (std::min)(height, rect.y + rect.height);
  for (int y = top; y < bottom; ++y) {
    for (int x = left; x < right; ++x) {
      setOverlayPixel(pixels, width, height, x, y, kHoverRevealPixel);
    }
  }
}

int decimalDigits(int value)
{
  int digits = 1;
  while (value >= 10) {
    value /= 10;
    ++digits;
  }
  return digits;
}

bool digitPixel(int digit, int column, int row)
{
  constexpr std::uint8_t kDigits[10][kHoverLabelGlyphHeight] = {
      {0b111, 0b101, 0b101, 0b101, 0b111},  // 0
      {0b010, 0b110, 0b010, 0b010, 0b111},  // 1
      {0b111, 0b001, 0b111, 0b100, 0b111},  // 2
      {0b111, 0b001, 0b111, 0b001, 0b111},  // 3
      {0b101, 0b101, 0b111, 0b001, 0b001},  // 4
      {0b111, 0b100, 0b111, 0b001, 0b111},  // 5
      {0b111, 0b100, 0b111, 0b101, 0b111},  // 6
      {0b111, 0b001, 0b010, 0b010, 0b010},  // 7
      {0b111, 0b101, 0b111, 0b101, 0b111},  // 8
      {0b111, 0b101, 0b111, 0b001, 0b111},  // 9
  };
  return digit >= 0 && digit <= 9 && column >= 0 &&
         column < kHoverLabelGlyphWidth && row >= 0 &&
         row < kHoverLabelGlyphHeight &&
         (kDigits[digit][row] & (1u << (kHoverLabelGlyphWidth - column - 1))) !=
             0;
}

void drawGlyphPixel(std::vector<std::uint32_t>& pixels, int width, int height,
                    int x, int y)
{
  for (int offset_y = 0; offset_y < kHoverLabelGlyphScale; ++offset_y) {
    for (int offset_x = 0; offset_x < kHoverLabelGlyphScale; ++offset_x) {
      setOverlayPixel(pixels, width, height, x + offset_x, y + offset_y,
                      kHoverLabelTextPixel);
    }
  }
}

void drawDigit(std::vector<std::uint32_t>& pixels, int width, int height,
               int x, int y, int digit)
{
  for (int row = 0; row < kHoverLabelGlyphHeight; ++row) {
    for (int column = 0; column < kHoverLabelGlyphWidth; ++column) {
      if (digitPixel(digit, column, row)) {
        drawGlyphPixel(pixels, width, height,
                       x + column * kHoverLabelGlyphScale,
                       y + row * kHoverLabelGlyphScale);
      }
    }
  }
}

void drawDimension(std::vector<std::uint32_t>& pixels, int width, int height,
                   int x, int y, int value)
{
  int divisor = 1;
  while (value / divisor >= 10) {
    divisor *= 10;
  }
  while (divisor > 0) {
    drawDigit(pixels, width, height, x, y, value / divisor);
    x += kHoverLabelGlyphWidth * kHoverLabelGlyphScale + kHoverLabelGlyphGap;
    value %= divisor;
    divisor /= 10;
  }
}

void drawMultiplySymbol(std::vector<std::uint32_t>& pixels, int width,
                        int height, int x, int y)
{
  for (int index = 0; index < kHoverLabelGlyphWidth; ++index) {
    drawGlyphPixel(pixels, width, height,
                   x + index * kHoverLabelGlyphScale,
                   y + index * kHoverLabelGlyphScale);
    drawGlyphPixel(pixels, width, height,
                   x + (kHoverLabelGlyphWidth - index - 1) *
                           kHoverLabelGlyphScale,
                   y + index * kHoverLabelGlyphScale);
  }
}

void drawHoverLabel(std::vector<std::uint32_t>& pixels, int width, int height,
                    const OverlayClientRect& rect)
{
  if (rect.width <= 0 || rect.height <= 0) {
    return;
  }
  const int glyph_advance =
      kHoverLabelGlyphWidth * kHoverLabelGlyphScale + kHoverLabelGlyphGap;
  const int text_glyph_count = decimalDigits(rect.width) + 1 +
                               decimalDigits(rect.height);
  const int panel_width = kHoverLabelPadding * 2 +
                          text_glyph_count * glyph_advance -
                          kHoverLabelGlyphGap;
  const int panel_height =
      kHoverLabelPadding * 2 + kHoverLabelGlyphHeight * kHoverLabelGlyphScale;
  const int below_y = rect.y + rect.height + kHoverLabelGap;
  const int above_y = rect.y - kHoverLabelGap - panel_height;
  int panel_y = 0;
  if (below_y + panel_height <= height) {
    panel_y = below_y;
  } else if (above_y >= 0) {
    panel_y = above_y;
  } else {
    return;
  }
  const int panel_x =
      (std::max)(0, (std::min)(rect.x, width - panel_width));

  for (int y = 0; y < panel_height; ++y) {
    for (int x = 0; x < panel_width; ++x) {
      setOverlayPixel(pixels, width, height, panel_x + x, panel_y + y,
                      kHoverLabelPanelPixel);
    }
  }

  int text_x = panel_x + kHoverLabelPadding;
  const int text_y = panel_y + kHoverLabelPadding;
  drawDimension(pixels, width, height, text_x, text_y, rect.width);
  text_x += decimalDigits(rect.width) * glyph_advance;
  drawMultiplySymbol(pixels, width, height, text_x, text_y);
  text_x += glyph_advance;
  drawDimension(pixels, width, height, text_x, text_y, rect.height);
}

constexpr int kMaxPreviewWidth = 440;
constexpr int kMaxPreviewHeight = 720;
constexpr int kPreviewPadding = 6;
constexpr int kPreviewGap = 14;
constexpr std::uint32_t kPreviewPanelPixel = 0xF0222222u;
constexpr std::uint32_t kPreviewPanelBorder = 0xFFFF8000u;

struct PreviewPanel {
  int x{0};
  int y{0};
  int width{0};
  int height{0};
  bool valid{false};
};

enum class PreviewPanelAnchor { Right, Left, Below, Above };

struct PreviewPanelCandidate {
  int x{0};
  int y{0};
  int width{0};
  int height{0};
  PreviewPanelAnchor anchor{PreviewPanelAnchor::Right};
};

PreviewPanel chooseLongShotPreviewPanel(int width, int height,
                                        const OverlayClientRect& selection) {
  const int selection_right = selection.x + selection.width;
  const int selection_bottom = selection.y + selection.height;
  const PreviewPanelCandidate candidates[] = {
      {selection_right + kPreviewGap, selection.y,
       width - selection_right - kPreviewGap, selection.height,
       PreviewPanelAnchor::Right},
      {0, selection.y, selection.x - kPreviewGap, selection.height,
       PreviewPanelAnchor::Left},
      {selection.x, selection_bottom + kPreviewGap, selection.width,
       height - selection_bottom - kPreviewGap, PreviewPanelAnchor::Below},
      {selection.x, 0, selection.width, selection.y - kPreviewGap,
       PreviewPanelAnchor::Above},
  };

  PreviewPanel panel;
  int best_area = 0;
  for (const PreviewPanelCandidate& candidate : candidates) {
    const int panel_width = (std::min)(
        candidate.width, kMaxPreviewWidth + kPreviewPadding * 2);
    const int panel_height = (std::min)(
        candidate.height, kMaxPreviewHeight + kPreviewPadding * 2);
    if (panel_width <= kPreviewPadding * 2 ||
        panel_height <= kPreviewPadding * 2) {
      continue;
    }

    const int area = panel_width * panel_height;
    if (area <= best_area) {
      continue;
    }

    PreviewPanel candidate_panel;
    candidate_panel.width = panel_width;
    candidate_panel.height = panel_height;
    switch (candidate.anchor) {
      case PreviewPanelAnchor::Right:
        candidate_panel.x = candidate.x;
        candidate_panel.y = candidate.y;
        break;
      case PreviewPanelAnchor::Left:
        candidate_panel.x = candidate.x + candidate.width - panel_width;
        candidate_panel.y = candidate.y;
        break;
      case PreviewPanelAnchor::Below:
        candidate_panel.x = candidate.x + candidate.width - panel_width;
        candidate_panel.y = candidate.y;
        break;
      case PreviewPanelAnchor::Above:
        candidate_panel.x = candidate.x + candidate.width - panel_width;
        candidate_panel.y = candidate.y + candidate.height - panel_height;
        break;
    }
    candidate_panel.valid = true;
    panel = candidate_panel;
    best_area = area;
  }
  return panel;
}

void drawLongShotPreview(std::vector<std::uint32_t>& pixels, int width,
                          int height, const OverlayClientRect& selection,
                          const Image& preview) {
  if (preview.empty() || width <= 0 || height <= 0) {
    return;
  }

  const PreviewPanel panel =
      chooseLongShotPreviewPanel(width, height, selection);
  if (!panel.valid) {
    return;
  }

  const int available_width = panel.width - kPreviewPadding * 2;
  const int available_height = panel.height - kPreviewPadding * 2;
  const std::int64_t width_for_full_height =
      static_cast<std::int64_t>(preview.width) * available_height /
      preview.height;
  int image_width = 0;
  int image_height = 0;
  if (width_for_full_height <= available_width) {
    image_width = (std::max)(1, static_cast<int>(width_for_full_height));
    image_height = available_height;
  } else {
    image_width = available_width;
    image_height = (std::max)(
        1, static_cast<int>(static_cast<std::int64_t>(preview.height) *
                            available_width / preview.width));
  }
  const int image_x = panel.x + kPreviewPadding +
                      (available_width - image_width) / 2;
  const int image_y = panel.y + kPreviewPadding +
                      (available_height - image_height) / 2;

  for (int y = 0; y < panel.height; ++y) {
    for (int x = 0; x < panel.width; ++x) {
      const bool border = x == 0 || y == 0 || x == panel.width - 1 ||
                          y == panel.height - 1;
      setOverlayPixel(pixels, width, height, panel.x + x, panel.y + y,
                      border ? kPreviewPanelBorder : kPreviewPanelPixel);
    }
  }

  for (int y = 0; y < image_height; ++y) {
    const int source_y = (std::min)(
        preview.height - 1,
        static_cast<int>(static_cast<std::int64_t>(y) * preview.height /
                         image_height));
    for (int x = 0; x < image_width; ++x) {
      const int source_x = (std::min)(
          preview.width - 1,
          static_cast<int>(static_cast<std::int64_t>(x) * preview.width /
                           image_width));
      setOverlayPixel(
          pixels, width, height, image_x + x, image_y + y,
          preview.pixels[static_cast<std::size_t>(source_y) *
                             static_cast<std::size_t>(preview.width) +
                         static_cast<std::size_t>(source_x)]);
    }
  }
}

}  // namespace

bool OverlayRenderer::renderPixels(
    int width, int height, const OverlayRenderState& state,
    std::vector<std::uint32_t>& out_pixels) {
  if (width <= 0 || height <= 0) {
    return false;
  }

  std::vector<std::uint32_t> pixels;
  mask::renderFullscreenMask(width, height, state.selection, pixels);
  const std::size_t num_pixels = static_cast<std::size_t>(width) *
                                 static_cast<std::size_t>(height);
  if (pixels.size() != num_pixels) {
    return false;
  }

  if (state.show_handles) {
    handles::drawHandles(pixels, width, height, state.selection.x,
                         state.selection.y, state.selection.width,
                         state.selection.height, kHandlePixel,
                         state.handle_radius);
  } else if (state.show_hover && !state.capture_passthrough &&
             state.phase == OverlayPhase::Sniffing) {
    revealHoverBackground(pixels, width, height, state.hover_rect);
    drawHoverGlow(pixels, width, height, state.hover_rect);
    drawHoverOutline(pixels, width, height, state.hover_rect, kHoverPixel,
                     kHoverThickness);
    drawHoverLabel(pixels, width, height, state.hover_rect);
  }

  // Draw before the capture passthrough clear so an impossible placement
  // (for example, a selection covering almost the whole screen) is clipped
  // out of the selected pixels and can never contaminate a captured frame.
  drawLongShotPreview(pixels, width, height, state.selection,
                      state.longshot_preview);

  if (state.capture_passthrough && overlayPhaseHasSelection(state.phase) &&
      !state.selection.empty()) {
    // Remove the border/handles as well as the hole's 1-alpha marker while a
    // worker captures. This keeps the selected pixels free of overlay UI.
    const int left = (std::max)(0, state.selection.x);
    const int top = (std::max)(0, state.selection.y);
    const int right =
        (std::min)(width, state.selection.x + state.selection.width);
    const int bottom =
        (std::min)(height, state.selection.y + state.selection.height);
    for (int y = top; y < bottom; ++y) {
      for (int x = left; x < right; ++x) {
        setOverlayPixel(pixels, width, height, x, y, 0x00000000u);
      }
    }
  }

  // 有桌面背景时：遮罩界面 = 背景截图 + 遮罩合成。其它窗口（含从属浮层）
  // 被包含在截图里，在遮罩界面中保持可见，不再被实时 topmost 窗口物理盖住。
  // 无背景（截屏失败）时退回纯遮罩，保留原 premultiplied alpha 行为。
  if (!state.capture_passthrough && !state.background.empty()) {
    std::vector<std::uint32_t> composed;
    if (!mask::composeBackground(state.background, pixels, composed)) {
      return false;
    }
    pixels.swap(composed);
  }

  out_pixels.swap(pixels);
  return true;
}

bool OverlayRenderer::render(HWND hwnd, const coord::VirtualScreenRect& screen,
                             const OverlayRenderState& state) {
  if (hwnd == nullptr || screen.width <= 0 || screen.height <= 0) {
    return false;
  }

  std::unique_ptr<HDC__, ScreenHdcDeleter> screen_dc(GetDC(nullptr));
  if (screen_dc == nullptr) {
    return false;
  }
  std::unique_ptr<HDC__, CompatibleDcDeleter> mem_dc(
      CreateCompatibleDC(screen_dc.get()));
  if (mem_dc == nullptr) {
    return false;
  }

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = screen.width;
  bmi.bmiHeader.biHeight = -screen.height;  // 顶向下
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void* dib_bits = nullptr;
  std::unique_ptr<HBITMAP__, DibDeleter> dib(CreateDIBSection(
      mem_dc.get(), &bmi, DIB_RGB_COLORS, &dib_bits, nullptr, 0));
  if (dib == nullptr || dib_bits == nullptr) {
    return false;
  }

  std::vector<std::uint32_t> pixels;
  if (!renderPixels(screen.width, screen.height, state, pixels)) {
    return false;
  }

  // DIB 与像素缓冲同布局（BGRA，顶向下），直接拷贝。
  std::copy(pixels.begin(), pixels.end(),
            reinterpret_cast<std::uint32_t*>(dib_bits));

  POINT dst{screen.x, screen.y};
  SIZE size{screen.width, screen.height};
  POINT pt_src{0, 0};
  BLENDFUNCTION blend{};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = 255;
  blend.AlphaFormat = AC_SRC_ALPHA;  // per-pixel alpha（premultiplied）

  const HGDIOBJ old_bitmap = SelectObject(mem_dc.get(), dib.get());
  if (old_bitmap == nullptr || old_bitmap == HGDI_ERROR) {
    return false;
  }
  const BOOL ok = UpdateLayeredWindow(hwnd, screen_dc.get(), &dst, &size,
                                      mem_dc.get(), &pt_src, 0, &blend,
                                      ULW_ALPHA);
  SelectObject(mem_dc.get(), old_bitmap);
  return ok != FALSE;
}

Image OverlayRenderer::makeLongShotPreviewImage(const Image& source) {
  if (source.empty()) {
    return Image{};
  }

  constexpr int kMaxWidth = 440;
  constexpr int kMaxHeight = 720;
  const int divisor = (std::max)(
      1, (std::max)((source.width + kMaxWidth - 1) / kMaxWidth,
                    (source.height + kMaxHeight - 1) / kMaxHeight));
  const int width = (std::max)(1, source.width / divisor);
  const int height = (std::max)(1, source.height / divisor);

  Image result;
  result.width = width;
  result.height = height;
  result.pixels.resize(static_cast<std::size_t>(width) *
                       static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    const int source_y = (std::min)(source.height - 1, y * divisor);
    for (int x = 0; x < width; ++x) {
      const int source_x = (std::min)(source.width - 1, x * divisor);
      result.pixels[static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(width) +
                    static_cast<std::size_t>(x)] =
          source.pixels[static_cast<std::size_t>(source_y) *
                            static_cast<std::size_t>(source.width) +
                        static_cast<std::size_t>(source_x)];
    }
  }
  return result;
}

}  // namespace qingying
