#pragma once

#include "qingying/automation/automation_types.hpp"

namespace qingying {

class IAutomationClient {
 public:
  virtual ~IAutomationClient() = default;

  // Each client instance is permanently bound to one authenticated epoch and
  // generation. Reconnect creates a NEW instance; a disconnected/closed client
  // rejects submissions. Never migrate or replay old requests on a new
  // connection, including after resolving a public handle to a numeric ID.
  // Exactly one completion per submit with a non-empty callback, including
  // validation/admission failure, exceptions and connection loss. Synchronous
  // completion is allowed; callers must establish tracking before submit.
  // execute completes after actual execution; begin_longshot completes once
  // the UI has admitted and started selection (or failed to start it).
  // Implementations document their completion executor; consumers must not
  // assume a UI thread or throw from the callback. Echo the originating
  // connection identity even if completion races reconnect/close.
  virtual void submit(AutomationRequest request,
                      AutomationCompletion completion) = 0;

  // Idempotently stop admission, cancel/drain pending work and settle all
  // callbacks before returning. Never exits the desktop application. Concrete
  // transport implementations own their I/O cancellation and worker cleanup.
  virtual void close() noexcept = 0;
};

}  // namespace qingying
