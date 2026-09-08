#include "qingying/mcp/mcp_protocol_session.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <map>

namespace qingying::mcp {
namespace {
using Json = nlohmann::json;

class LifecycleClient final : public IAutomationClient {
 public:
  LifecycleClient() : prefix_("client_" + std::to_string(++next_client_) + "_") {}
  void submit(AutomationRequest request, AutomationCompletion completion) override {
    AutomationResponse response;
    response.result.request_id = request.request_id;
    response.result.operation_id = request.request_id;
    response.result.ok = true;
    response.result.error_code = ErrorCode::kOk;
    if (const auto* execute = std::get_if<ExecuteActionRequest>(&request.payload)) {
      if (const auto* crop = std::get_if<CropCenterRequest>(&execute->payload)) {
        const ResultId id = next_result_++;
        const std::string handle = prefix_ + "result_" + std::to_string(id);
        results_[handle] = id;
        response.result.output = CapturedResult{id, crop->width, crop->height,
            {0, 0, crop->width, crop->height}};
        response.result_handle = ResultHandle{handle};
        response.operation_handle = OperationHandle{"crop_operation"};
      } else if (const auto* save = std::get_if<SaveRequest>(&execute->payload)) {
        const auto found = execute->result_handle
            ? results_.find(execute->result_handle->value) : results_.end();
        if (found == results_.end()) {
          response.result.ok = false;
          response.result.error_code = ErrorCode::kResultNotFound;
        } else {
          response.result.output = SavedResult{found->second, save->path};
          response.operation_handle = OperationHandle{"save_operation"};
        }
      }
    } else if (const auto* release = std::get_if<ReleaseResultRequest>(&request.payload)) {
      response.result.operation_id = 0;
      const auto found = release->result_handle
          ? results_.find(release->result_handle->value) : results_.end();
      if (found == results_.end()) {
        response.result.ok = false;
        response.result.error_code = ErrorCode::kResultNotFound;
      } else {
        response.control = ReleasedResult{found->second, false};
        results_.erase(found);
      }
    }
    completion(std::move(response));
  }
  void close() noexcept override {}

 private:
  inline static std::atomic<unsigned> next_client_{0};
  std::string prefix_;
  ResultId next_result_{1};
  std::map<std::string, ResultId> results_;
};

Json take(McpProtocolSession& session) {
  auto line = session.takeOutput();
  return line ? Json::parse(*line) : Json{};
}
void ready(McpProtocolSession& session) {
  session.receive(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"lifecycle-test","version":"1"}}})");
  ASSERT_TRUE(take(session).contains("result"));
  session.receive(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
}
void call(McpProtocolSession& session, int id, const char* name, Json arguments) {
  session.receive(Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", "tools/call"},
      {"params", {{"name", name}, {"arguments", std::move(arguments)}}}}.dump());
}

TEST(McpResultLifecycleIntegrationTest,
     CaptureAThenBMaySaveAndReleaseAByOpaqueHandle) {
  McpProtocolSession first_session(std::make_shared<LifecycleClient>());
  McpProtocolSession second_session(std::make_shared<LifecycleClient>());
  ready(first_session);
  ready(second_session);
  call(first_session, 2, "crop_center", {{"width", 8}, {"height", 6}});
  const auto first = take(first_session)["result"]["structuredContent"];
  call(second_session, 3, "crop_center", {{"width", 4}, {"height", 2}});
  const auto second = take(second_session)["result"]["structuredContent"];
  ASSERT_NE(first["result_id"], second["result_id"]);

  call(first_session, 4, "save", {{"result_id", first["result_id"]},
      {"path", "C:\\allowed"}, {"name", "first.png"},
      {"request_key", "save-first"}});
  const auto saved = take(first_session)["result"];
  EXPECT_FALSE(saved["isError"].get<bool>());
  EXPECT_EQ(saved["structuredContent"]["result_id"], first["result_id"]);
  EXPECT_EQ(saved["structuredContent"]["format"], "png");

  call(first_session, 5, "release_result", {{"result_id", first["result_id"]}});
  EXPECT_FALSE(take(first_session)["result"]["isError"].get<bool>());
  call(first_session, 6, "save", {{"result_id", first["result_id"]},
      {"path", "C:\\allowed"}, {"name", "again.png"}});
  const auto rejected = take(first_session)["result"];
  EXPECT_TRUE(rejected["isError"].get<bool>());
  EXPECT_EQ(rejected["structuredContent"]["error"]["code"],
            "ResultNotFound");
}

}  // namespace
}  // namespace qingying::mcp
