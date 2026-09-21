#include "qingying/export/export_service.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <Windows.h>

#include "qingying/export/dib_encoder.hpp"
#include "png_file_transaction.h"

namespace qingying {

namespace {

struct GlobalMemoryDeleter {
  void operator()(void* handle) const {
    if (handle != nullptr) {
      ::GlobalFree(static_cast<HGLOBAL>(handle));
    }
  }
};

using ClipboardMemory = std::unique_ptr<void, GlobalMemoryDeleter>;

ClipboardMemory copyIntoGlobalMemory(const dib::DibBytes& bytes) {
  ClipboardMemory memory(::GlobalAlloc(GMEM_MOVEABLE, bytes.size()));
  if (!memory) return {};
  void* const locked = ::GlobalLock(memory.get());
  if (locked == nullptr) return {};
  std::memcpy(locked, bytes.data(), bytes.size());
  // Single GlobalLock pairs with a single GlobalUnlock. A FALSE return is
  // benign when the lock count reaches zero, so no GetLastError handling here.
  (void)::GlobalUnlock(memory.get());
  return memory;
}

ActionResult makeExportError(const char* message) {
  ActionResult r;
  r.ok = false;
  r.error_code = ErrorCode::kExportFailed;
  r.message = message;
  return r;
}

}  // namespace

ActionResult ExportService::copyToClipboard(const Image& image) try {
  const auto dib_bytes = dib::encodeDib(image);
  const auto dib_v5_bytes = dib::encodeDibV5(image);
  if (dib_bytes.empty() || dib_v5_bytes.empty()) {
    return makeExportError("ExportService::copyToClipboard: 空图，无内容可复制");
  }

  const auto total_bytes = static_cast<std::uint64_t>(dib_bytes.size()) +
                           static_cast<std::uint64_t>(dib_v5_bytes.size());
  auto clipboard_memory = ImageMemoryBudget::global().reserve(
      total_bytes, ImageMemoryKind::WireCopy);
  if (!clipboard_memory) throw std::bad_alloc{};

  // Both formats are allocated and populated before opening the clipboard, so
  // local allocation failures leave the user's existing clipboard untouched.
  ClipboardMemory legacy = copyIntoGlobalMemory(dib_bytes);
  ClipboardMemory modern = copyIntoGlobalMemory(dib_v5_bytes);
  if (!legacy || !modern) {
    return makeExportError("ExportService::copyToClipboard: GlobalAlloc 失败");
  }

  if (!::OpenClipboard(nullptr)) {
    return makeExportError("ExportService::copyToClipboard: OpenClipboard 失败");
  }

  if (!::EmptyClipboard()) {
    ::CloseClipboard();
    return makeExportError("ExportService::copyToClipboard: EmptyClipboard 失败");
  }

  if (::SetClipboardData(CF_DIB, legacy.get()) == nullptr) {
    // 失败：系统未接管句柄，交由 unique_ptr 的删除器 GlobalFree。
    ::CloseClipboard();
    return makeExportError("ExportService::copyToClipboard: SetClipboardData 失败");
  }

  // CF_DIB keeps Paint and legacy Office compatibility. CF_DIBV5 declares
  // channel masks and alpha for Chromium and modern Office/WPS consumers. A
  // V5 publication failure does not invalidate the already-copied CF_DIB.
  legacy.release();
  if (::SetClipboardData(CF_DIBV5, modern.get()) != nullptr) {
    modern.release();
  }
  ::CloseClipboard();

  ActionResult r;
  r.ok = true;
  r.error_code = ErrorCode::kOk;
  r.message = "图像已复制到剪贴板（CF_DIB / CF_DIBV5）";
  return r;
} catch (const std::bad_alloc&) {
  ActionResult result;
  result.error_code = ErrorCode::kResourceLimit;
  result.message = "image memory budget exhausted";
  return result;
}

ActionResult ExportService::savePng(const Image& image,
                                    const std::wstring& path) {
  PngSaveOptions options;
  options.overwrite = true;
  return detail::savePngFileTransaction(image, path, std::move(options));
}

ActionResult ExportService::savePng(const Image& image,
                                    const std::wstring& path,
                                    PngSaveOptions options) {
  return detail::savePngFileTransaction(image, path, std::move(options));
}

}  // namespace qingying
