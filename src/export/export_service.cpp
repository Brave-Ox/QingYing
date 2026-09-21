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
  if (dib_bytes.empty()) {
    return makeExportError("ExportService::copyToClipboard: 空图，无内容可复制");
  }

  auto clipboard_memory = ImageMemoryBudget::global().reserve(dib_bytes.size(), ImageMemoryKind::WireCopy);
  if (!clipboard_memory) throw std::bad_alloc{};

  // 先完成可能失败的分配和写入；避免因本地内存不足而清空用户原有剪贴板。
  std::unique_ptr<void, GlobalMemoryDeleter> h_mem(
      ::GlobalAlloc(GMEM_MOVEABLE, dib_bytes.size()));
  if (!h_mem) {
    return makeExportError("ExportService::copyToClipboard: GlobalAlloc 失败");
  }

  void* const locked = ::GlobalLock(h_mem.get());
  if (locked == nullptr) {
    return makeExportError("ExportService::copyToClipboard: GlobalLock 失败");
  }
  std::memcpy(locked, dib_bytes.data(), dib_bytes.size());
  // 单次 GlobalLock 配对单次 GlobalUnlock：此时返回 FALSE 仅为"仍有锁计数"
  // 的良性情况（GetLastError()==NO_ERROR），无需按错误处理。
  (void)::GlobalUnlock(h_mem.get());

  if (!::OpenClipboard(nullptr)) {
    return makeExportError("ExportService::copyToClipboard: OpenClipboard 失败");
  }

  if (!::EmptyClipboard()) {
    ::CloseClipboard();
    return makeExportError("ExportService::copyToClipboard: EmptyClipboard 失败");
  }

  if (::SetClipboardData(CF_DIB, h_mem.get()) == nullptr) {
    // 失败：系统未接管句柄，交由 unique_ptr 的删除器 GlobalFree。
    ::CloseClipboard();
    return makeExportError("ExportService::copyToClipboard: SetClipboardData 失败");
  }

  // 成功：系统接管 h_mem 所有权，release 防止二次 GlobalFree。
  h_mem.release();
  ::CloseClipboard();

  ActionResult r;
  r.ok = true;
  r.error_code = ErrorCode::kOk;
  r.message = "图像已复制到剪贴板";
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
