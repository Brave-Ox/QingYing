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
constexpr int kHoverThickness = 3;

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
                      const SelectionResult& rect, std::uint32_t color,
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

bool rectanglesIntersect(int left_a, int top_a, int right_a, int bottom_a,
                         int left_b, int top_b, int right_b, int bottom_b) {
  return left_a < right_b && left_b < right_a && top_a < bottom_b &&
         top_b < bottom_a;
}

void drawLongShotPreview(std::vector<std::uint32_t>& pixels, int width,
                         int height, const SelectionResult& selection,
                         const Image& preview) {
  if (preview.empty() || width <= 0 || height <= 0) {
    return;
  }

  constexpr int kMaxPreviewWidth = 440;
  constexpr int kMaxPreviewHeight = 720;
  constexpr int kPreviewPadding = 6;
  constexpr int kPreviewGap = 14;
  constexpr std::uint32_t kPanelPixel = 0xF0222222u;
  constexpr std::uint32_t kPanelBorder = 0xFFFF8000u;

  const int scale_x = kMaxPreviewWidth / preview.width;
  const int scale_y = kMaxPreviewHeight / preview.height;
  const int scale = (std::max)(1, (std::min)(scale_x, scale_y));
  const int image_width =
      (std::max)(1, (std::min)(kMaxPreviewWidth, preview.width * scale));
  const int image_height =
      (std::max)(1, (std::min)(kMaxPreviewHeight, preview.height * scale));
  const int panel_width = image_width + kPreviewPadding * 2;
  const int panel_height = image_height + kPreviewPadding * 2;

  const int selection_left = selection.x;
  const int selection_top = selection.y;
  const int selection_right = selection.x + selection.width;
  const int selection_bottom = selection.y + selection.height;

  struct Candidate {
    int x;
    int y;
  };
  const Candidate candidates[] = {
      {selection_right + kPreviewGap, selection_top},
      {selection_left - panel_width - kPreviewGap, selection_top},
      {selection_left, selection_bottom + kPreviewGap},
      {selection_left, selection_top - panel_height - kPreviewGap},
      {(width - panel_width) / 2, (height - panel_height) / 2},
  };

  int panel_x = candidates[0].x;
  int panel_y = candidates[0].y;
  for (const Candidate& candidate : candidates) {
    const int right = candidate.x + panel_width;
    const int bottom = candidate.y + panel_height;
    if (candidate.x >= 0 && candidate.y >= 0 && right <= width &&
        bottom <= height &&
        !rectanglesIntersect(candidate.x, candidate.y, right, bottom,
                             selection_left, selection_top, selection_right,
                             selection_bottom)) {
      panel_x = candidate.x;
      panel_y = candidate.y;
      break;
    }
  }

  panel_x = (std::max)(0, (std::min)(panel_x, width - panel_width));
  panel_y = (std::max)(0, (std::min)(panel_y, height - panel_height));

  for (int y = 0; y < panel_height; ++y) {
    for (int x = 0; x < panel_width; ++x) {
      const bool border = x == 0 || y == 0 || x == panel_width - 1 ||
                          y == panel_height - 1;
      setOverlayPixel(pixels, width, height, panel_x + x, panel_y + y,
                      border ? kPanelBorder : kPanelPixel);
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
          pixels, width, height, panel_x + kPreviewPadding + x,
          panel_y + kPreviewPadding + y,
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
  } else if (state.show_hover) {
    drawHoverOutline(pixels, width, height, state.hover_rect, kHoverPixel,
                     kHoverThickness);
  }

  // Draw before the capture passthrough clear so an impossible placement
  // (for example, a selection covering almost the whole screen) is clipped
  // out of the selected pixels and can never contaminate a captured frame.
  drawLongShotPreview(pixels, width, height, state.selection,
                      state.longshot_preview);

  if (state.capture_passthrough &&
      overlayPhaseHasSelection(state.phase) && !state.selection.cancelled) {
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

  POINT dst{screen.left, screen.top};
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
