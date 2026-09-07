#pragma once

#include "qingying/automation/operation_registry.h"
#include "qingying/app/ui_message_channel.h"

#include <map>

namespace qingying {

// Construct and dispatch/drain on the UI thread. Other methods are thread-safe.
// Post must enqueue a message (never dispatch inline), return false on failure,
// and must not throw or reenter the scheduler. Join producers before destruction.
// Bind Post to PostMessage(hwnd, message, 0, token) != FALSE and forward these
// two WM_APP messages to dispatch(message, LPARAM). Call drain periodically on
// UI to recover failed completion posts and disconnected queued requests.
// Capacity covers queued AND running requests until completion delivery, so
// request-control associations and reserved completion slots remain bounded.
// Completion callbacks run outside locks: rejection on the submitting thread,
// accepted requests on the UI thread. Callbacks must not throw.
class UiActionScheduler final {
 public:
  using Post = std::function<bool(UINT, UiMessageToken)>;
  using Execute = std::function<void(UiMessageToken, const TrustedAutomationContext&,
      const AutomationRequest&, std::shared_ptr<OperationControl>)>;
  UiActionScheduler(Post post, Execute execute, AutomationLimits limits = {},
                    OperationClock clock = {});
  ~UiActionScheduler();
  bool connect(const TrustedAutomationContext& context);
  void disconnect(const TrustedAutomationContext& context);
  void submit(TrustedAutomationContext context, AutomationRequest request,
              AutomationCompletion completion);
  bool cancel(const TrustedAutomationContext& context, RequestId request,
              AbortReason reason = AbortReason::ClientCancel);
  // One completion slot is reserved for every admitted request, including when
  // PostMessage fails. drain() recovers such events on the UI thread.
  bool complete(UiMessageToken ticket, AutomationResponse response);
  void dispatch(UINT message, UiMessageToken token);
  void stopAccepting();
  void drain();
  // Final callback reclamation, after stopping/joining business producers and
  // reporting their actual outcomes. Never blocks waiting for workers.
  void shutdown();
  std::size_t pending() const;

 private:
  struct Entry {
    TrustedAutomationContext context;
    AutomationRequest request;
    AutomationCompletion completion;
    std::shared_ptr<OperationControl> control;
    bool control_lane{false};
    bool running{false};
    UiMessageToken completion_token{0};
    std::optional<AutomationResponse> response;
  };
  bool connected(const TrustedAutomationContext& context) const;
  void checkThread() const;
  void settle(UiMessageToken ticket);
  void abortQueued(AbortReason reason, const TrustedAutomationContext* context);
  static AutomationResponse failure(const Entry& entry, int code);
  Post post_;
  Execute execute_;
  AutomationLimits limits_;
  OperationClock clock_;
  std::thread::id ui_thread_;
  mutable std::mutex mutex_;
  bool accepting_{true};
  ApplicationEpoch epoch_{0};
  ConnectionGeneration last_generation_{0};
  std::map<ConnectionGeneration, ResultScopeId> connections_;
  std::map<UiMessageToken, Entry> entries_;
  UiMessageChannel channel_;
};
}  // namespace qingying
