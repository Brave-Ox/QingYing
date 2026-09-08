#include "qingying/app/automation_settings.h"
#include "qingying/app/mcp_stdio_runner.h"
#include "qingying/app/tray_context_menu.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace qingying {
namespace {
using Json = nlohmann::json;
bool until(const std::function<bool()>& predicate, DWORD timeout = 7000) {
  const auto start = GetTickCount64();
  do {
    if (predicate()) return true;
    Sleep(10);
  } while (GetTickCount64() - start < timeout);
  return false;
}
void closeHandle(HANDLE& handle) {
  if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
  handle = nullptr;
}
// Own only the child and handles created by this test. Explicit inheritance
// prevents another bridge from keeping this bridge's stdin alive after EOF.
class Child {
 public:
  ~Child() {
    eof();
    if (process && WaitForSingleObject(process, 1000) == WAIT_TIMEOUT) {
      if (auto window = hwnd()) PostMessageW(window, WM_CLOSE, 0, 0);
      if (WaitForSingleObject(process, 7000) == WAIT_TIMEOUT) {
        ADD_FAILURE() << "test child did not shut down";
        TerminateProcess(process, 99);
        WaitForSingleObject(process, 2000);
      }
    }
    closeHandle(process); closeHandle(output); closeHandle(error);
  }
  bool start(const std::wstring& arguments, bool production = false) {
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    std::wstring exe(path);
    exe.resize(exe.find_last_of(L"\\/") + 1);
    exe += production ? L"qingying.exe" : L"qingying_process_fixture.exe";
    std::wstring command = L"\"" + exe + L"\" " + arguments;
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE child_input{}, child_output{}, child_error{};
    if (!CreatePipe(&child_input, &input, &security, 0) ||
        !CreatePipe(&output, &child_output, &security, 0) ||
        !CreatePipe(&error, &child_error, &security, 0)) return false;
    SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(error, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.StartupInfo.hStdInput = child_input;
    startup.StartupInfo.hStdOutput = child_output;
    startup.StartupInfo.hStdError = child_error;
    SIZE_T size{};
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<unsigned char> attributes(size);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size)) return false;
    HANDLE inherited[]{child_input, child_output, child_error};
    const bool configured = UpdateProcThreadAttribute(startup.lpAttributeList, 0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr) != FALSE;
    PROCESS_INFORMATION info{};
    const bool ok = configured && CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr, &startup.StartupInfo, &info);
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    closeHandle(child_input); closeHandle(child_output); closeHandle(child_error);
    if (!ok) return false;
    process = info.hProcess; pid = info.dwProcessId; CloseHandle(info.hThread);
    return true;
  }
  HWND hwnd() const {
    struct Search { DWORD pid; HWND window{}; } search{pid};
    EnumWindows([](HWND window, LPARAM context) -> BOOL {
      auto& search = *reinterpret_cast<Search*>(context);
      DWORD pid{}; GetWindowThreadProcessId(window, &pid);
      wchar_t name[128]{}; GetClassNameW(window, name, 128);
      if (pid == search.pid && std::wstring(name) == L"QingYing.TrayHiddenWindow") search.window = window;
      return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.window;
  }
  bool send(const Json& value) {
    const auto text = value.dump() + "\n";
    DWORD written{};
    return WriteFile(input, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
  }
  Json read() {
    if (!until([&] {
      DWORD available{};
      if (PeekNamedPipe(output, nullptr, 0, nullptr, &available, nullptr) && available) {
        char bytes[4096]; DWORD got{};
        if (ReadFile(output, bytes, (std::min)(available, DWORD(sizeof(bytes))), &got, nullptr)) buffer.append(bytes, got);
      }
      return buffer.find('\n') != std::string::npos;
    })) return {};
    const auto end = buffer.find('\n');
    const auto line = buffer.substr(0, end); buffer.erase(0, end + 1);
    return Json::parse(line, nullptr, false);
  }
  void initialize() {
    ASSERT_TRUE(send({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
        {"params", {{"protocolVersion", "2024-11-05"}, {"capabilities", Json::object()},
                    {"clientInfo", {{"name", "process-test"}, {"version", "1"}}}}}}));
    ASSERT_TRUE(read().contains("result"));
    ASSERT_TRUE(send({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}));
  }
  Json status() {
    if (!send({{"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"},
        {"params", {{"name", "status"}, {"arguments", Json::object()}}}})) return {};
    return read();
  }
  void eof() { closeHandle(input); }
  DWORD outputBytes() const {
    DWORD bytes{}; PeekNamedPipe(output, nullptr, 0, nullptr, &bytes, nullptr); return bytes;
  }
  bool exited(DWORD timeout = 7000) { return WaitForSingleObject(process, timeout) == WAIT_OBJECT_0; }
  DWORD exitCode() { DWORD code{}; GetExitCodeProcess(process, &code); return code; }
  bool message(UINT message, WPARAM parameter = 0) {
    DWORD_PTR result{};
    return SendMessageTimeoutW(hwnd(), message, parameter, 0, SMTO_ABORTIFHUNG, 7000, &result) != 0;
  }
 private:
  HANDLE process{}, input{}, output{}, error{};
  DWORD pid{};
  std::string buffer;
};
class McpProcessIntegrationTest : public ::testing::Test {
 protected:
  std::wstring scope = L"process_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
  AutomationSettings settings{scope};
  std::wstring args(bool stdio = false) { return L"--test-scope=" + scope + (stdio ? L" --mcp-stdio" : L""); }
  void TearDown() override { RegDeleteKeyW(HKEY_CURRENT_USER, settings.key().c_str()); }
};
TEST(McpLaunchModeTest, AcceptsOnlyEmptyOrSoleStdioArgument) {
  EXPECT_EQ(parseLaunchMode({}), LaunchMode::Tray);
  EXPECT_EQ(parseLaunchMode({L"--mcp-stdio"}), LaunchMode::McpStdio);
  EXPECT_EQ(parseLaunchMode({L"--mcp-stdio", L"extra"}), LaunchMode::Invalid);
  EXPECT_EQ(parseLaunchMode({L"未知参数"}), LaunchMode::Invalid);
}
TEST_F(McpProcessIntegrationTest, ProductionRejectsUnknownAndTestFlagsWithoutGui) {
  for (const auto& argument : {std::wstring(L"--unknown"), args()}) {
    Child child; ASSERT_TRUE(child.start(argument, true));
    ASSERT_TRUE(child.exited()); EXPECT_EQ(child.exitCode(), 2u); EXPECT_EQ(child.hwnd(), nullptr);
  }
}
TEST_F(McpProcessIntegrationTest, MissingGuiStillInitializesAndListsTools) {
  EXPECT_FALSE(settings.enabled());
  Child bridge; ASSERT_TRUE(bridge.start(args(true))); bridge.initialize();
  ASSERT_TRUE(bridge.send({{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}}));
  EXPECT_TRUE(bridge.read().contains("result"));
  const auto status = bridge.status();
  EXPECT_NE(status.dump().find("pipe_not_found"), std::string::npos) << status;
  EXPECT_EQ(bridge.hwnd(), nullptr);
  bridge.eof(); ASSERT_TRUE(bridge.exited()); EXPECT_EQ(bridge.exitCode(), 0u);
}
TEST_F(McpProcessIntegrationTest, TwoBridgesEofDisableEnableAndRestartAreIndependent) {
  Child gui; ASSERT_TRUE(gui.start(args())); ASSERT_TRUE(until([&] { return gui.hwnd() != nullptr; }));
  ASSERT_TRUE(gui.message(WM_NULL));
  {
    Child off; ASSERT_TRUE(off.start(args(true))); off.initialize();
    EXPECT_NE(off.status().dump().find("pipe_not_found"), std::string::npos);
  }
  ASSERT_TRUE(gui.message(WM_COMMAND, TrayMenuAutomationCommandId));
  ASSERT_TRUE(settings.enabled());
  Child first, second;
  ASSERT_TRUE(first.start(args(true))); first.initialize();
  ASSERT_TRUE(second.start(args(true))); second.initialize();
  auto status = second.status();
  EXPECT_NE(status.dump().find("\"automation_enabled\":true"), std::string::npos) << status;
  EXPECT_EQ(first.hwnd(), nullptr); EXPECT_EQ(second.hwnd(), nullptr);
  first.eof(); ASSERT_TRUE(first.exited()); EXPECT_EQ(first.exitCode(), 0u);
  EXPECT_NE(second.status().dump().find("\"automation_enabled\":true"), std::string::npos);
  ASSERT_TRUE(gui.message(WM_COMMAND, TrayMenuAutomationCommandId));
  EXPECT_FALSE(settings.enabled());
  EXPECT_EQ(second.status().dump().find("\"automation_enabled\":true"), std::string::npos);
  ASSERT_TRUE(gui.message(WM_COMMAND, TrayMenuAutomationCommandId));
  {
    Child fresh; ASSERT_TRUE(fresh.start(args(true))); fresh.initialize();
    EXPECT_NE(fresh.status().dump().find("\"automation_enabled\":true"), std::string::npos);
  }
  ASSERT_TRUE(gui.message(WM_CLOSE)); ASSERT_TRUE(gui.exited()); EXPECT_EQ(gui.exitCode(), 0u);
  // The still-running bridge must not retain the fixture GUI's hotkey.
  const bool hotkey_free = RegisterHotKey(nullptr, 0xF911, MOD_CONTROL | MOD_SHIFT, VK_F24) != FALSE;
  EXPECT_TRUE(hotkey_free);
  if (hotkey_free) UnregisterHotKey(nullptr, 0xF911);
  Child restarted; ASSERT_TRUE(restarted.start(args())); ASSERT_TRUE(until([&] { return restarted.hwnd() != nullptr; }));
  ASSERT_TRUE(restarted.message(WM_NULL));
  Child fresh; ASSERT_TRUE(fresh.start(args(true))); fresh.initialize();
  EXPECT_NE(fresh.status().dump().find("\"automation_enabled\":true"), std::string::npos);
  ASSERT_TRUE(restarted.message(WM_ENDSESSION, TRUE)); ASSERT_TRUE(restarted.exited());
}
TEST_F(McpProcessIntegrationTest, SlowStdoutDoesNotPreventGuiShutdown) {
  ASSERT_TRUE(settings.setEnabled(true));
  Child gui; ASSERT_TRUE(gui.start(args())); ASSERT_TRUE(until([&] { return gui.hwnd() != nullptr; }));
  ASSERT_TRUE(gui.message(WM_NULL));
  Child bridge; ASSERT_TRUE(bridge.start(args(true))); bridge.initialize();
  // Several valid catalog responses fill the 4 KiB anonymous pipe while
  // remaining below the session's 16-frame queue limit.
  for (int id = 10; id < 20; ++id)
    ASSERT_TRUE(bridge.send({{"jsonrpc", "2.0"}, {"id", id}, {"method", "tools/list"}}));
  ASSERT_TRUE(until([&] { return bridge.outputBytes() >= 4096; }));
  ASSERT_TRUE(gui.message(WM_COMMAND, TrayMenuExitCommandId));
  ASSERT_TRUE(gui.exited());
  bridge.eof(); ASSERT_TRUE(bridge.exited()); EXPECT_EQ(bridge.exitCode(), 4u);
}
}  // namespace
}  // namespace qingying
