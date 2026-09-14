#include "qingying/app/application_shutdown_coordinator.h"

#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace qingying {
namespace {

using namespace std::chrono_literals;

TEST(ApplicationShutdownCoordinatorTest, RunsFourPhasesInOrderOnlyOnce) {
  std::vector<ApplicationShutdownPhase> phases;
  int calls = 0;
  ApplicationShutdownCoordinator coordinator(
      {
          {ApplicationShutdownPhase::StopAdmission, "admission",
           [&](ApplicationShutdownDeadline) {
             ++calls;
             phases.push_back(ApplicationShutdownPhase::StopAdmission);
             return true;
           },
           {}},
          {ApplicationShutdownPhase::RejectNewWork, "queue",
           [&](ApplicationShutdownDeadline) {
             ++calls;
             phases.push_back(ApplicationShutdownPhase::RejectNewWork);
             return true;
           },
           {}},
          {ApplicationShutdownPhase::CancelAndWait, "worker",
           [&](ApplicationShutdownDeadline) {
             ++calls;
             phases.push_back(ApplicationShutdownPhase::CancelAndWait);
             return true;
           },
           {}},
          {ApplicationShutdownPhase::DrainAndDestroy, "callbacks",
           [&](ApplicationShutdownDeadline) {
             ++calls;
             phases.push_back(ApplicationShutdownPhase::DrainAndDestroy);
             return true;
           },
           {}},
      });

  const auto first = coordinator.shutdown();
  const auto second = coordinator.shutdown();

  ASSERT_TRUE(first.completed);
  EXPECT_FALSE(first.deadline_exceeded);
  EXPECT_EQ(calls, 4);
  ASSERT_EQ(phases.size(), 4u);
  EXPECT_EQ(phases[0], ApplicationShutdownPhase::StopAdmission);
  EXPECT_EQ(phases[1], ApplicationShutdownPhase::RejectNewWork);
  EXPECT_EQ(phases[2], ApplicationShutdownPhase::CancelAndWait);
  EXPECT_EQ(phases[3], ApplicationShutdownPhase::DrainAndDestroy);
  EXPECT_EQ(second.diagnostics.size(), first.diagnostics.size());
}

TEST(ApplicationShutdownCoordinatorTest, RecordsBudgetOverrunAndSnapshot) {
  ApplicationShutdownCoordinator coordinator(
      {{ApplicationShutdownPhase::CancelAndWait, "blocked_export",
        [](ApplicationShutdownDeadline) {
          std::this_thread::sleep_for(5ms);
          return true;
        },
        [] { return std::string("queued=2 running=1"); }}},
      ApplicationShutdownCoordinator::Options{1ms, {}});

  const auto report = coordinator.shutdown();

  ASSERT_TRUE(report.completed);
  ASSERT_TRUE(report.deadline_exceeded);
  ASSERT_EQ(report.diagnostics.size(), 1u);
  EXPECT_TRUE(report.diagnostics.front().deadline_exceeded);
  EXPECT_NE(report.diagnostics.front().detail.find("queued=2"),
            std::string::npos);
}

TEST(ApplicationShutdownCoordinatorTest, ContinuesAfterParticipantException) {
  bool second_called = false;
  ApplicationShutdownCoordinator coordinator(
      {
          {ApplicationShutdownPhase::RejectNewWork, "throws",
           [](ApplicationShutdownDeadline) -> bool {
             throw std::runtime_error("test shutdown failure");
           },
           {}},
          {ApplicationShutdownPhase::DrainAndDestroy, "continues",
           [&](ApplicationShutdownDeadline) {
             second_called = true;
             return true;
           },
           {}},
      });

  const auto report = coordinator.shutdown();

  EXPECT_FALSE(report.completed);
  EXPECT_TRUE(second_called);
  ASSERT_EQ(report.diagnostics.size(), 2u);
  EXPECT_NE(report.diagnostics.front().detail.find("test shutdown failure"),
            std::string::npos);
}

TEST(ApplicationShutdownCoordinatorTest,
     StopsDestructiveCleanupAfterParticipantMissesDeadline) {
  bool cleanup_called = false;
  ApplicationShutdownCoordinator coordinator(
      {
          {ApplicationShutdownPhase::CancelAndWait, "blocked_worker",
           [](ApplicationShutdownDeadline) { return false; }, {}},
          {ApplicationShutdownPhase::DrainAndDestroy, "destroy_callbacks",
           [&](ApplicationShutdownDeadline) {
             cleanup_called = true;
             return true;
           }, {}},
      });

  const auto report = coordinator.shutdown();

  EXPECT_FALSE(report.completed);
  EXPECT_FALSE(cleanup_called);
  ASSERT_EQ(report.diagnostics.size(), 1u);
  EXPECT_FALSE(report.diagnostics.front().completed);
  EXPECT_EQ(report.diagnostics.front().detail,
            "participant did not finish before deadline");
}

TEST(ApplicationShutdownCoordinatorTest,
     ContinuesWaitingForIndependentWorkersAfterOneTimesOut) {
  bool second_worker_called = false;
  bool cleanup_called = false;
  ApplicationShutdownCoordinator coordinator(
      {
          {ApplicationShutdownPhase::CancelAndWait, "blocked_worker",
           [](ApplicationShutdownDeadline) { return false; }, {}, 10ms},
          {ApplicationShutdownPhase::CancelAndWait, "independent_worker",
           [&](ApplicationShutdownDeadline) {
             second_worker_called = true;
             return true;
           }, {}, 20ms},
          {ApplicationShutdownPhase::DrainAndDestroy, "destroy_callbacks",
           [&](ApplicationShutdownDeadline) {
             cleanup_called = true;
             return true;
           }, {}},
      });

  const auto report = coordinator.shutdown();

  EXPECT_FALSE(report.completed);
  EXPECT_TRUE(second_worker_called);
  EXPECT_FALSE(cleanup_called);
  ASSERT_EQ(report.diagnostics.size(), 2u);
  EXPECT_EQ(report.diagnostics[0].budget, 10ms);
  EXPECT_LE(report.diagnostics[1].budget, 20ms);
}

}  // namespace
}  // namespace qingying
