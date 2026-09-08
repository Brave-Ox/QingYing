#include "qingying/app/save_policy.h"

#include <Windows.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <utility>

namespace qingying {
namespace {

ActionResult failure(int code, const char* message) {
  ActionResult result;
  result.error_code = code;
  result.message = message;
  result.failure_stage = "path_policy";
  return result;
}

ActionResult success() {
  ActionResult result;
  result.ok = true;
  result.error_code = ErrorCode::kOk;
  return result;
}

std::wstring fullPath(const std::wstring& value) {
  if (value.empty()) return {};
  const DWORD needed = GetFullPathNameW(value.c_str(), 0, nullptr, nullptr);
  if (needed == 0 || needed > 32768) return {};
  std::wstring result(needed, L'\0');
  const DWORD length = GetFullPathNameW(value.c_str(), needed, result.data(), nullptr);
  if (length == 0 || length >= needed) return {};
  result.resize(length);
  return std::filesystem::path(result).lexically_normal().wstring();
}

std::wstring lower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return value;
}

bool localDosPath(const std::wstring& value) {
  if (value.size() < 3 || value[1] != L':' ||
      (value[2] != L'\\' && value[2] != L'/')) return false;
  if (!std::iswalpha(value[0])) return false;
  return value.rfind(L"\\\\", 0) != 0 && value.rfind(L"//", 0) != 0 &&
      value.rfind(L"\\\\?\\", 0) != 0 && value.rfind(L"\\\\.\\", 0) != 0;
}

bool inside(const std::wstring& child, const std::wstring& root) {
  const auto child_lower = lower(child);
  auto root_lower = lower(root);
  while (root_lower.size() > 3 &&
         (root_lower.back() == L'\\' || root_lower.back() == L'/')) root_lower.pop_back();
  if (child_lower == root_lower) return true;
  return child_lower.size() > root_lower.size() &&
      child_lower.compare(0, root_lower.size(), root_lower) == 0 &&
      (child_lower[root_lower.size()] == L'\\' || child_lower[root_lower.size()] == L'/');
}

bool safeName(const std::wstring& name) {
  if (name.empty() || name == L"." || name == L".." || name.size() > 255 ||
      name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos ||
      name.back() == L'.' || name.back() == L' ') return false;
  if (std::filesystem::path(name).filename().wstring() != name) return false;
  const auto dot = name.find(L'.');
  const auto stem = lower(name.substr(0, dot));
  if (stem == L"con" || stem == L"prn" || stem == L"aux" || stem == L"nul")
    return false;
  if (stem.size() == 4 && (stem.rfind(L"com", 0) == 0 || stem.rfind(L"lpt", 0) == 0) &&
      stem[3] >= L'1' && stem[3] <= L'9') return false;
  return lower(std::filesystem::path(name).extension().wstring()) == L".png";
}

bool directoryChainIsStable(const std::wstring& directory) {
  std::filesystem::path current = std::filesystem::path(directory).root_path();
  const auto relative = std::filesystem::path(directory).relative_path();
  for (const auto& component : relative) {
    current /= component;
    const DWORD attributes = GetFileAttributesW(current.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return false;
  }
  HANDLE handle = CreateFileW(directory.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  BY_HANDLE_FILE_INFORMATION identity{};
  const bool valid_identity = GetFileInformationByHandle(handle, &identity) != FALSE &&
      (identity.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
      (identity.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
  CloseHandle(handle);
  return valid_identity;
}

}  // namespace

SavePolicy::SavePolicy(std::vector<std::wstring> allowed_directories) {
  allowed_directories_.reserve(allowed_directories.size());
  for (auto& directory : allowed_directories) {
    auto absolute = fullPath(directory);
    if (localDosPath(absolute)) allowed_directories_.push_back(std::move(absolute));
  }
}

ActionResult SavePolicy::validate(const std::wstring& directory,
                                  const std::wstring& name, bool overwrite,
                                  ValidatedSavePath* output) const {
  if (output == nullptr) return failure(ErrorCode::kInvalidArgument,
      "save policy output is required");
  *output = {};
  if (!safeName(name)) return failure(ErrorCode::kInvalidArgument,
      "name must be one non-reserved PNG filename");
  const auto absolute_directory = fullPath(directory);
  if (!localDosPath(absolute_directory)) return failure(ErrorCode::kAccessDenied,
      "only local DOS directories are allowed");
  const auto allowed = std::find_if(allowed_directories_.begin(),
      allowed_directories_.end(), [&](const auto& root) {
        return inside(absolute_directory, root) && directoryChainIsStable(root) &&
               directoryChainIsStable(absolute_directory);
      });
  if (allowed == allowed_directories_.end()) return failure(ErrorCode::kAccessDenied,
      "output directory is outside allowed roots or uses a reparse point");
  output->absolute_path =
      (std::filesystem::path(absolute_directory) / name).lexically_normal().wstring();
  output->overwrite = overwrite;
  return success();
}

ActionResult SavePolicy::validateFullPath(const std::wstring& path,
                                          bool overwrite,
                                          ValidatedSavePath* output) const {
  if (!localDosPath(path) || path.find(L':', 2) != std::wstring::npos)
    return failure(ErrorCode::kAccessDenied,
        "UNC, device and alternate-data-stream paths are not allowed");
  const std::filesystem::path input(path);
  return validate(input.parent_path().wstring(), input.filename().wstring(),
                  overwrite, output);
}

}  // namespace qingying
