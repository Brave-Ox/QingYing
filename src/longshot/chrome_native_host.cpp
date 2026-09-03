#include <Windows.h>
#include <wincrypt.h>

#include <string>
#include <vector>

namespace {
constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\QingYingBrowserCaptureV1";

bool readExact(HANDLE handle, void* buffer, DWORD bytes) {
  BYTE* cursor = static_cast<BYTE*>(buffer);
  DWORD total = 0;
  while (total < bytes) {
    DWORD read = 0;
    if (!ReadFile(handle, cursor + total, bytes - total, &read, nullptr) || read == 0) return false;
    total += read;
  }
  return true;
}

bool writeResponse(bool ok, const char* message) {
  const std::string json = std::string("{\"ok\":") + (ok ? "true" : "false") +
      ",\"message\":\"" + message + "\"}";
  const DWORD size = static_cast<DWORD>(json.size());
  DWORD written = 0;
  return WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), &size, sizeof(size), &written, nullptr) &&
      WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), json.data(), size, &written, nullptr);
}

bool extractDataUrl(const std::string& json, std::string& data_url) {
  constexpr char key[] = "\"data\":\"";
  const size_t start = json.find(key);
  if (start == std::string::npos) return false;
  const size_t value = start + sizeof(key) - 1;
  const size_t end = json.find('"', value);
  if (end == std::string::npos) return false;
  data_url = json.substr(value, end - value);
  return data_url.rfind("data:image/png;base64,", 0) == 0;
}

bool writeTempPng(const std::string& data_url, std::wstring& path) {
  const std::string base64 = data_url.substr(std::string("data:image/png;base64,").size());
  DWORD bytes = 0;
  if (!CryptStringToBinaryA(base64.data(), static_cast<DWORD>(base64.size()),
      CRYPT_STRING_BASE64, nullptr, &bytes, nullptr, nullptr) || bytes == 0) return false;
  std::vector<BYTE> png(bytes);
  if (!CryptStringToBinaryA(base64.data(), static_cast<DWORD>(base64.size()),
      CRYPT_STRING_BASE64, png.data(), &bytes, nullptr, nullptr)) return false;
  wchar_t directory[MAX_PATH] = {};
  wchar_t temporary[MAX_PATH] = {};
  if (GetTempPathW(MAX_PATH, directory) == 0 || GetTempFileNameW(directory, L"QYB", 0, temporary) == 0) return false;
  path = temporary;
  path += L".png";
  if (!MoveFileW(temporary, path.c_str())) return false;
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = WriteFile(file, png.data(), bytes, &written, nullptr) && written == bytes;
  CloseHandle(file);
  if (!ok) DeleteFileW(path.c_str());
  return ok;
}

bool deliverToQingYing(const std::wstring& path) {
  HANDLE pipe = CreateFileW(kPipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) return false;
  const int count = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (count <= 1) return false;
  // WideCharToMultiByte 的 -1 输入长度会连同结尾 NUL 一起计数。
  // 先留足 count 字节再移除 NUL，避免向 count - 1 的缓冲区写入 count 字节，
  // 否则 Native Messaging 偶发崩溃，浏览器截图无法交给主程序。
  std::string utf8(static_cast<size_t>(count), '\0');
  if (WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8.data(), count,
                          nullptr, nullptr) == 0) {
    return false;
  }
  utf8.pop_back();
  const DWORD length = static_cast<DWORD>(utf8.size());
  DWORD written = 0;
  const bool ok = WriteFile(pipe, &length, sizeof(length), &written, nullptr) &&
      WriteFile(pipe, utf8.data(), length, &written, nullptr);
  CloseHandle(pipe);
  return ok;
}
}  // namespace

int main() {
  DWORD length = 0;
  if (!readExact(GetStdHandle(STD_INPUT_HANDLE), &length, sizeof(length)) || length == 0 || length > 100 * 1024 * 1024) return 1;
  std::string request(length, '\0');
  std::string data_url;
  if (!readExact(GetStdHandle(STD_INPUT_HANDLE), request.data(), length) || !extractDataUrl(request, data_url)) {
    return writeResponse(false, "invalid request") ? 1 : 1;
  }
  std::wstring path;
  if (!writeTempPng(data_url, path) || !deliverToQingYing(path)) {
    if (!path.empty()) DeleteFileW(path.c_str());
    return writeResponse(false, "QingYing is not running") ? 1 : 1;
  }
  return writeResponse(true, "imported") ? 0 : 1;
}
