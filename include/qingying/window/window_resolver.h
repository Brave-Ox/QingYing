#pragma once

#include "qingying/action/types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace qingying {

enum class WindowTitleMatch { Contains, Exact };

struct WindowQuery {
  std::wstring title;
  WindowTitleMatch match{WindowTitleMatch::Contains};
  std::optional<std::uint32_t> process_id;
};

// Native identity stays inside the application. Public MCP responses use only
// WindowCandidate::window_token and never serialize native_handle.
struct WindowCatalogEntry {
  std::uintptr_t native_handle{0};
  std::uint32_t process_id{0};
  std::wstring title;
  ScreenPhysicalRect bounds{};
};

struct WindowCatalogSnapshot {
  std::vector<WindowCatalogEntry> entries;
  bool truncated{false};
};

struct ResolvedWindow {
  WindowCatalogEntry identity;
  std::string window_token;
};

struct WindowResolveResult {
  int error_code{ErrorCode::kUnknown};
  std::optional<ResolvedWindow> window;
  WindowCandidates candidates;
  bool ok() const noexcept { return error_code == ErrorCode::kOk; }
};

class WindowResolver final {
 public:
  using Catalog = std::function<WindowCatalogSnapshot()>;

  explicit WindowResolver(Catalog catalog = {}, std::size_t candidate_limit = 8);
  WindowResolveResult resolve(const WindowQuery& query) const;

  // F9-17 calls this immediately before capture. It rejects a closed/reused
  // handle, changed process identity, or changed visible bounds.
  bool revalidate(const ResolvedWindow& expected,
                  ResolvedWindow* current = nullptr) const;

 private:
  Catalog catalog_;
  std::size_t candidate_limit_;
};

WindowCatalogSnapshot enumerateCaptureWindows();

}  // namespace qingying
