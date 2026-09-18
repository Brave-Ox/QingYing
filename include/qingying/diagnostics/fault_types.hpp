#pragma once

#include <array>
#include <chrono>
#include <cstdint>

namespace qingying {
enum class FaultOrigin { Ui, Scheduler, Pipe, Handler, Plugin, Worker };
enum class FaultDomain { Request, Session, Provider, Application };

// Trusted execution metadata, never populated from client arguments.
struct FaultContext {
  std::uint64_t epoch{0};
  std::uint64_t session_id{0};
  std::uint64_t scope_id{0};
  std::uint64_t request_id{0};
  std::uint64_t operation_id{0};
  std::chrono::steady_clock::time_point started_at{};
};
struct FaultDiagnostic {
  int error_code{1};
  FaultOrigin origin{FaultOrigin::Ui};
  FaultDomain domain{FaultDomain::Request};
  bool retryable{false};
  FaultContext context;
  std::uint64_t elapsed_ms{0};
  std::array<char, 96> correlation_id{};
  std::array<char, 64> provider_id{};
};
const char* faultOriginName(FaultOrigin value) noexcept;
const char* faultDomainName(FaultDomain value) noexcept;
}  // namespace qingying
