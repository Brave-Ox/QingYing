#pragma once
#include "qingying/automation/automation_endpoint.h"
#include "qingying/ipc/pipe_server.h"
namespace qingying {
// Owner-thread composition. The window/message route must outlive shutdown.
class AutomationRuntime final {
 public:
  AutomationRuntime(AutomationEndpoint& endpoint, UiActionScheduler& scheduler,
                    ipc::PipeOptions options = {});
  ~AutomationRuntime();
  bool enable();
  void disable();
  void tick();
  void shutdown();
  bool enabled() const { return pipe_ != nullptr; }
 private:
  AutomationEndpoint& endpoint_;
  UiActionScheduler& scheduler_;
  ipc::PipeOptions options_;
  std::unique_ptr<ipc::PipeServer> pipe_;
  bool stopped_{false};
};
}  // namespace qingying
