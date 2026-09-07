#pragma once

#include "qingying/automation/automation_contract.h"

#include <mutex>
#include <thread>
#include <unordered_map>

namespace qingying {

using OperationClock = std::function<std::chrono::steady_clock::time_point()>;

// May be created before queue admission and passed unchanged into begin().
// The injected monotonic clock must be thread-safe and must not throw.
class OperationControl final : public IOperationControl {
 public:
  OperationControl(TrustedAutomationContext context, RequestId request_id,
                   std::chrono::milliseconds timeout, OperationClock clock = {});
  bool requestCancel(AbortReason reason) override;
  bool tryCommit() override;
  OperationControlStatus status() override;

 private:
  friend class OperationRegistry;
  void checkAbortLocked();
  bool claim(const TrustedAutomationContext& context, const AutomationRequest& request,
             std::chrono::milliseconds timeout);
  std::optional<ActionResult> settle(ActionResult outcome, bool requires_commit);
  std::mutex mutex_;
  TrustedAutomationContext context_;
  RequestId request_id_;
  std::chrono::milliseconds timeout_;
  std::chrono::steady_clock::time_point deadline_;
  OperationClock clock_;
  OperationControlStatus status_;
  bool claimed_{false};
};

struct OperationSubmission {
  int error_code{ErrorCode::kUnknown};
  bool reused{false};
  OperationId operation_id{kInvalidOperationId};
  OperationHandle handle;
  std::shared_ptr<IOperationControl> control;
  bool ok() const noexcept { return error_code == ErrorCode::kOk; }
};

// All methods except calls through an issued IOperationControl belong to the
// constructing UI thread. Records retain bounded metadata and execution
// controls, never Image, ResultLease, or execution/completion callbacks.
class OperationRegistry {
 public:
  explicit OperationRegistry(ApplicationEpoch epoch, AutomationLimits limits = {},
                             OperationClock clock = {});
  ~OperationRegistry();
  OperationRegistry(const OperationRegistry&) = delete;
  OperationRegistry& operator=(const OperationRegistry&) = delete;

  std::optional<TrustedAutomationContext> connect(ResultScopeId scope);
  void disconnect(const TrustedAutomationContext& context);
  OperationSubmission begin(const TrustedAutomationContext& context,
      const AutomationRequest& request, std::shared_ptr<OperationControl> control = {});
  std::optional<OperationSnapshot> get(const TrustedAutomationContext& context,
                                      OperationId id);
  std::optional<OperationSnapshot> get(const TrustedAutomationContext& context,
                                      const OperationHandle& handle);
  std::optional<CancellationResult> cancel(const TrustedAutomationContext& context,
      OperationId id, AbortReason reason = AbortReason::ClientCancel);
  bool advance(const TrustedAutomationContext& context, OperationId id,
               OperationState state, OperationProgress progress = {});
  bool complete(const TrustedAutomationContext& context, OperationId id,
                ActionResult outcome, ActionResult* settled_outcome = nullptr);
  ResultId currentResult(const TrustedAutomationContext& context);
  std::optional<ResultHandle> bindResult(const TrustedAutomationContext& context,
                                         ResultId id);
  std::optional<ResultId> resolveResult(const TrustedAutomationContext& context,
      const ResultHandle& handle, bool include_invalidated = false);
  // include_invalidated is for release/status routing only. Resolving a handle
  // never replaces ResultStore's scope/expiry checks before consuming pixels.
  void invalidateResult(const TrustedAutomationContext& context, ResultId id,
                        ResultAvailability availability);
  // Deadline requests cancellation only; the executor still acknowledges it.
  void sweep();
  std::size_t recordCount() const;

 private:
  struct Session {
    struct InvalidResult {
      ResultId id;
      ResultHandle handle;
      std::chrono::steady_clock::time_point invalidated_at;
    };
    ResultScopeId scope;
    bool connected{true};
    ResultId result_id{kInvalidResultId};
    ResultHandle result_handle;
    std::vector<InvalidResult> invalid_results;
  };
  struct Record {
    AutomationConnection connection;
    ResultScopeId scope;
    RequestId request_id;
    OperationSnapshot snapshot;
    OperationHandle handle;
    std::shared_ptr<OperationControl> control;
    std::optional<std::string> request_key;
    std::string canonical_parameters;
    bool requires_commit{false};
  };
  void checkThread() const;
  Session* session(const TrustedAutomationContext& context, bool connected = true);
  Record* record(const TrustedAutomationContext& context, OperationId id,
                 bool connected = true);
  void refresh(Record& record);
  void trim();
  std::string newHandle() const;
  OperationSubmission submission(const Record& record, bool reused) const;

  ApplicationEpoch epoch_;
  AutomationLimits limits_;
  OperationClock clock_;
  std::thread::id ui_thread_;
  ConnectionGeneration next_generation_{1};
  OperationId next_operation_{1};
  std::unordered_map<ConnectionGeneration, Session> sessions_;
  std::unordered_map<OperationId, Record> records_;
};

}  // namespace qingying
