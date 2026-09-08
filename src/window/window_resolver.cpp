#include "qingying/window/window_resolver.h"

#include "window_query_helpers.h"

#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace qingying {
namespace {
std::atomic<std::uint64_t> next_token{1};

bool lessEntry(const WindowCatalogEntry& left,
               const WindowCatalogEntry& right) {
  const int title = CompareStringOrdinal(left.title.data(),
      static_cast<int>(left.title.size()), right.title.data(),
      static_cast<int>(right.title.size()), TRUE);
  if (title != CSTR_EQUAL) return title == CSTR_LESS_THAN;
  if (left.process_id != right.process_id)
    return left.process_id < right.process_id;
  if (left.bounds.x != right.bounds.x) return left.bounds.x < right.bounds.x;
  if (left.bounds.y != right.bounds.y) return left.bounds.y < right.bounds.y;
  if (left.bounds.width != right.bounds.width)
    return left.bounds.width < right.bounds.width;
  if (left.bounds.height != right.bounds.height)
    return left.bounds.height < right.bounds.height;
  return left.native_handle < right.native_handle;
}

std::string token() {
  return "window_" + std::to_string(next_token.fetch_add(1));
}
WindowCandidate candidate(const WindowCatalogEntry& entry,
                          const std::string& value) {
  return {entry.title, entry.process_id, entry.bounds, value};
}
}  // namespace

WindowResolver::WindowResolver(Catalog catalog, std::size_t candidate_limit)
    : catalog_(catalog ? std::move(catalog) : Catalog{enumerateCaptureWindows}),
      candidate_limit_(candidate_limit) {
  if (candidate_limit_ == 0) throw std::invalid_argument("candidate limit");
}

WindowResolveResult WindowResolver::resolve(const WindowQuery& query) const {
  WindowResolveResult result;
  if (query.title.empty()) {
    result.error_code = ErrorCode::kInvalidArgument;
    return result;
  }
  auto snapshot = catalog_();
  std::vector<WindowCatalogEntry> matches;
  for (const auto& entry : snapshot.entries) {
    if (entry.native_handle == 0 || entry.process_id == 0 ||
        entry.title.empty() || !entry.bounds.valid()) continue;
    if (query.process_id && entry.process_id != *query.process_id) continue;
    if (window_detail::invariantTitleMatch(entry.title, query.title,
        query.match == WindowTitleMatch::Exact)) matches.push_back(entry);
  }
  std::sort(matches.begin(), matches.end(), lessEntry);
  if (matches.empty()) {
    result.error_code = ErrorCode::kWindowNotFound;
    result.candidates.truncated = snapshot.truncated;
    return result;
  }
  if (matches.size() == 1 && !snapshot.truncated) {
    result.error_code = ErrorCode::kOk;
    result.window = ResolvedWindow{matches.front(), token()};
    result.candidates.candidates.push_back(
        candidate(matches.front(), result.window->window_token));
    return result;
  }
  result.error_code = ErrorCode::kWindowAmbiguous;
  result.candidates.truncated = snapshot.truncated ||
      matches.size() > candidate_limit_;
  const auto count = (std::min)(matches.size(), candidate_limit_);
  for (std::size_t i = 0; i < count; ++i)
    result.candidates.candidates.push_back(candidate(matches[i], token()));
  return result;
}

bool WindowResolver::revalidate(const ResolvedWindow& expected,
                                ResolvedWindow* current) const {
  if (current) *current = {};
  const auto snapshot = catalog_();
  for (const auto& entry : snapshot.entries) {
    if (entry.native_handle != expected.identity.native_handle) continue;
    if (entry.process_id != expected.identity.process_id ||
        entry.title != expected.identity.title ||
        entry.bounds != expected.identity.bounds) return false;
    if (current) *current = ResolvedWindow{entry, expected.window_token};
    return true;
  }
  return false;
}

}  // namespace qingying
