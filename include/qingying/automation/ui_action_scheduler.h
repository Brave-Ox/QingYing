#pragma once

#include "qingying/automation/operation_registry.h"
#include "qingying/app/ui_message_channel.h"

#include <map>

namespace qingying {

// Construct and dispatch/drain on the UI thread. Other methods are thread-safe.
// Post must enqueue a message (never dispatch inline), return false on failure,
// and must not throw or reenter the scheduler. Join producers before destruction.
// Bind Post to PostMessage(hwnd, message, 0, token) != FALSE and forward these
// WM_QINGYING_AUTOMATION_WAKE to dispatch(message, LPARAM). One pending wake
// covers a bounded batch of requests/results. Low-frequency housekeeping may
// call drain to recover a failed wake; normal progress never waits for a timer.
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
  // Thread-safe, coalesced notification for transport and scheduler events.
  bool notify();
  void stopAccepting();
  void drain();
  // Final callback reclamation, after stopping/joining business producers and
  // reporting their actual outcomes. Never blocks waiting for workers.
  void shutdown();
  std::size_t pending() const;
  QueueUsage queueUsage() const;
  // Receipt captured at admission survives the target request's completion.
  std::optional<CancellationResult> cancellationReceipt(UiMessageToken ticket) const;
  // UI binds an admitted idempotent retry to the original operation control.
  void shareControl(UiMessageToken ticket, std::shared_ptr<OperationControl> control);
  // UI-only hooks: finalize business metadata before delivery, release its
  // interaction guard after delivery (also across nested message pumps).
  void setSettlementHooks(
      std::function<void(UiMessageToken, AutomationResponse&)> before,
      std::function<void(UiMessageToken)> after);

 private:
  // Terminal business states live in OperationRegistry; scheduler entries are
  // retired atomically at settlement, before any consumer callback runs.
  enum class Phase { Queued, Running, Settling };
  struct Entry {
    TrustedAutomationContext context;
    AutomationRequest request;
    AutomationCompletion completion;
    std::shared_ptr<OperationControl> control;
    bool control_lane{false};
    Phase phase{Phase::Queued};
    UiMessageToken completion_token{0};
    std::optional<AutomationResponse> response;
    std::optional<CancellationResult> cancellation_receipt;
  };
  bool connected(const TrustedAutomationContext& context) const;
  void checkThread() const;
  void settle(UiMessageToken ticket);
  void executeRequest(UiMessageToken ticket);
  bool notifyLocked();
  void abortQueued(AbortReason reason, const TrustedAutomationContext* context);
  static AutomationResponse failure(const Entry& entry, int code);
  Post post_;
  Execute execute_;
  std::function<void(UiMessageToken, AutomationResponse&)> before_settlement_;
  std::function<void(UiMessageToken)> after_settlement_;
  AutomationLimits limits_;
  OperationClock clock_;
  std::thread::id ui_thread_;
  mutable std::mutex mutex_;
  bool accepting_{true};
  bool stopped_{false};
  bool draining_{false};
  bool wake_needed_{false};
  UiMessageToken wake_token_{0};
  ApplicationEpoch epoch_{0};
  ConnectionGeneration last_generation_{0};
  std::map<ConnectionGeneration, ResultScopeId> connections_;
  std::map<UiMessageToken, Entry> entries_;
  UiMessageChannel channel_;
};
}  // namespace qingying
