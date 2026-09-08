#include "qingying/app/mcp_stdio_runner.h"
#include "qingying/ipc/pipe_automation_client.h"
#include "qingying/mcp/stdio_transport.h"
#include <Windows.h>
namespace qingying {
LaunchMode parseLaunchMode(const std::vector<std::wstring>& arguments) {
  if (arguments.empty()) return LaunchMode::Tray;
  if (arguments.size() == 1 && arguments.front() == L"--mcp-stdio") return LaunchMode::McpStdio;
  return LaunchMode::Invalid;
}
namespace {
class UnavailableClient final : public IAutomationClient {
 public:
  explicit UnavailableClient(DWORD error) : reason_(error == ERROR_FILE_NOT_FOUND ? "pipe_not_found" :
      error == ERROR_ACCESS_DENIED ? "pipe_access_denied" :
      error == ERROR_PIPE_BUSY ? "pipe_busy" : "pipe_connection_failed") {}
  void submit(AutomationRequest request, AutomationCompletion completion) override {
    AutomationResponse response;
    response.transport_available = false;
    response.result.request_id = request.request_id;
    response.result.error_code = ErrorCode::kNotReady;
    response.result.message = reason_;
    if (completion) completion(std::move(response));
  }
  void close() noexcept override {}
 private:
  std::string reason_;
};
}
int runMcpStdio(const std::wstring& test_namespace) {
  try {
    ipc::PipeOptions options; options.test_suffix = test_namespace;
    auto pipe = std::make_shared<ipc::PipeAutomationClient>(options);
    std::shared_ptr<IAutomationClient> client;
    if (pipe->connect()) client = pipe;
    else client = std::make_shared<UnavailableClient>(pipe->lastError());
    McpBridge bridge(std::move(client));
    return mcp::StdioTransport{}.run(bridge) ? 0 : 4;
  } catch (...) { return 4; }
}
}  // namespace qingying
