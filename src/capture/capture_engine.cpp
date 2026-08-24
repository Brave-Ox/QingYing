#include <Windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "qingying/capture/capture_engine.hpp"

namespace qingying {

namespace {

// DIB 布局：32bpp BGRA，每像素 4 字节。
constexpr std::uint32_t kAlphaOpaqueChannel = 0xFFu;
constexpr int kBytesPerPixel = 4;

// 屏幕 DC：GetDC 获得的系统画布必须用 ReleaseDC 归还。
struct ScreenHdcReleaser {
  void operator()(HDC hdc) const noexcept {
    if (hdc != nullptr) {
      ReleaseDC(nullptr, hdc);
    }
  }
};

// CreateCompatibleDC 创建的 DC 用 DeleteDC 释放。
struct CreatedHdcDeleter {
  void operator()(HDC hdc) const noexcept {
    if (hdc != nullptr) {
      DeleteDC(hdc);
    }
  }
};

// CreateDIBSection 创建的 HBITMAP 用 DeleteObject 释放。
struct DibDeleter {
  void operator()(HBITMAP bitmap) const noexcept {
    if (bitmap != nullptr) {
      DeleteObject(bitmap);
    }
  }
};

// 把 DIB（32bpp BGRA、顶向下、stride 对齐到 4 字节）拷贝进 Image，
// 并强制 alpha 通道不透明（GDI 的 32bpp 位图 alpha 位默认为 0）。
void copyDibToImage(const std::uint8_t* dib_bits, int width, int height,
                    int stride_bytes, Image& out) {
  out.width = width;
  out.height = height;
  out.pixels.resize(static_cast<std::size_t>(width) *
                    static_cast<std::size_t>(height));

  for (int row = 0; row < height; ++row) {
    const std::size_t src_base =
        static_cast<std::size_t>(row) * static_cast<std::size_t>(stride_bytes);
    const std::size_t dst_base =
        static_cast<std::size_t>(row) * static_cast<std::size_t>(width);
    for (int col = 0; col < width; ++col) {
      const std::size_t src =
          src_base + static_cast<std::size_t>(col) * kBytesPerPixel;
      const std::size_t dst = dst_base + static_cast<std::size_t>(col);
      const std::uint32_t b = dib_bits[src + 0];
      const std::uint32_t g = dib_bits[src + 1];
      const std::uint32_t r = dib_bits[src + 2];
      out.pixels.at(dst) =
          (kAlphaOpaqueChannel << 24) | (r << 16) | (g << 8) | b;
    }
  }
}

}  // namespace

struct CaptureEngine::Impl {
  // GDI 截屏不跨调用持有状态；预留 DXGI 桌面复制接入位。
};

CaptureEngine::CaptureEngine() : impl_(new Impl) {}

CaptureEngine::~CaptureEngine() {
  delete impl_;
  impl_ = nullptr;
}

ActionResult CaptureEngine::captureRegion(int x, int y, int width, int height,
                                          Image& out) {
  ActionResult r;
  if (width <= 0 || height <= 0) {
    r.ok = false;
    r.error_code = ErrorCode::kInvalidArgument;
    r.message = "captureRegion: width/height must be positive";
    out = Image{};
    return r;
  }

  const int screen_w = GetSystemMetrics(SM_CXSCREEN);
  const int screen_h = GetSystemMetrics(SM_CYSCREEN);

  // 与屏幕可见区求交：负坐标/越界都收敛到屏幕内，得到实际可拷贝的源区域。
  const std::int64_t x1 = static_cast<std::int64_t>(x);
  const std::int64_t y1 = static_cast<std::int64_t>(y);
  const std::int64_t x2 = x1 + static_cast<std::int64_t>(width);
  const std::int64_t y2 = y1 + static_cast<std::int64_t>(height);
  const std::int64_t e_x1 = (x1 > 0) ? x1 : 0;
  const std::int64_t e_y1 = (y1 > 0) ? y1 : 0;
  const std::int64_t e_x2 =
      (x2 < static_cast<std::int64_t>(screen_w))
          ? x2
          : static_cast<std::int64_t>(screen_w);
  const std::int64_t e_y2 =
      (y2 < static_cast<std::int64_t>(screen_h))
          ? y2
          : static_cast<std::int64_t>(screen_h);
  const std::int64_t e_w = e_x2 - e_x1;
  const std::int64_t e_h = e_y2 - e_y1;

  std::unique_ptr<HDC__, ScreenHdcReleaser> screen_dc(GetDC(nullptr));
  if (screen_dc == nullptr) {
    r.ok = false;
    r.error_code = ErrorCode::kCaptureFailed;
    r.message = "captureRegion: GetDC failed";
    out = Image{};
    return r;
  }

  std::unique_ptr<HDC__, CreatedHdcDeleter> mem_dc(
      CreateCompatibleDC(screen_dc.get()));
  if (mem_dc == nullptr) {
    r.ok = false;
    r.error_code = ErrorCode::kCaptureFailed;
    r.message = "captureRegion: CreateCompatibleDC failed";
    out = Image{};
    return r;
  }

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width;
  bmi.bmiHeader.biHeight = -height;  // 负数 = 顶向下，与屏幕坐标一致
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void* dib_bits_raw = nullptr;
  std::unique_ptr<HBITMAP__, DibDeleter> dib(CreateDIBSection(
      mem_dc.get(), &bmi, DIB_RGB_COLORS, &dib_bits_raw, nullptr, 0));
  if (dib == nullptr || dib_bits_raw == nullptr) {
    r.ok = false;
    r.error_code = ErrorCode::kCaptureFailed;
    r.message = "captureRegion: CreateDIBSection failed";
    out = Image{};
    return r;
  }
  const auto* dib_bits = static_cast<const std::uint8_t*>(dib_bits_raw);

  if (e_w <= 0 || e_h <= 0) {
    // 区域完全落在屏幕外：返回不透明黑图，保持宽高契约。
    out.width = width;
    out.height = height;
    out.pixels.assign(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
        (kAlphaOpaqueChannel << 24));
    r.ok = true;
    r.error_code = ErrorCode::kOk;
    return r;
  }

  // 选入 DIB → BitBlt → 立即换回，避免位图仍挂载在 DC 上导致释放失败。
  const HGDIOBJ old_bitmap = SelectObject(mem_dc.get(), dib.get());
  if (old_bitmap == nullptr || old_bitmap == HGDI_ERROR) {
    r.ok = false;
    r.error_code = ErrorCode::kCaptureFailed;
    r.message = "captureRegion: SelectObject failed";
    out = Image{};
    return r;
  }

  const int dst_x = static_cast<int>(e_x1 - x1);
  const int dst_y = static_cast<int>(e_y1 - y1);
  const BOOL blt_ok =
      BitBlt(mem_dc.get(), dst_x, dst_y, static_cast<int>(e_w),
             static_cast<int>(e_h), screen_dc.get(), static_cast<int>(e_x1),
             static_cast<int>(e_y1), SRCCOPY);
  SelectObject(mem_dc.get(), old_bitmap);
  if (!blt_ok) {
    r.ok = false;
    r.error_code = ErrorCode::kCaptureFailed;
    r.message = "captureRegion: BitBlt failed";
    out = Image{};
    return r;
  }

  const int stride_bytes = width * kBytesPerPixel;
  copyDibToImage(dib_bits, width, height, stride_bytes, out);
  r.ok = true;
  r.error_code = ErrorCode::kOk;
  return r;
}

ActionResult CaptureEngine::captureWindow(const std::wstring& /*query*/,
                                          Image& /*out*/) {
  ActionResult r;
  r.ok = false;
  r.error_code = ErrorCode::kNotImplemented;
  r.message = "CaptureEngine::captureWindow stub";
  return r;
}

ActionResult CaptureEngine::cropCenter(int /*width*/, int /*height*/,
                                       Image& /*out*/) {
  ActionResult r;
  r.ok = false;
  r.error_code = ErrorCode::kNotImplemented;
  r.message = "CaptureEngine::cropCenter stub";
  return r;
}

}  // namespace qingying
