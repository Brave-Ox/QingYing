#include "qingying/app/interaction_gate.h"
#include "qingying/app/result_action_service.h"
#include "qingying/export/export_service.hpp"
#include "qingying/pin/pin_manager.hpp"
#include <gtest/gtest.h>

namespace qingying {
TEST(InteractionGateTest, OnlyExplicitOwnerCanNestAndLastGuardReleases) {
  InteractionGate gate, other;
  auto capture = gate.acquire(InteractionKind::Capture);
  ASSERT_TRUE(capture);
  EXPECT_FALSE(gate.acquire(InteractionKind::Capture));
  EXPECT_FALSE(other.acquire(InteractionKind::SaveDialog, &capture));
  auto dialog = gate.acquire(InteractionKind::SaveDialog, &capture);
  ASSERT_TRUE(dialog);
  capture.setKind(InteractionKind::LongShot);
  EXPECT_STREQ(gate.reason(), "longshot");
  capture.reset();
  EXPECT_TRUE(gate.busy());
  EXPECT_FALSE(gate.acquire(InteractionKind::Copy));
  dialog.reset();
  EXPECT_FALSE(gate.busy());
  EXPECT_TRUE(gate.acquire(InteractionKind::Pin));
}
TEST(InteractionGateTest, StopRejectsAdmissionWithoutPretendingCleanupFinished) {
  InteractionGate gate;
  auto capture = gate.acquire(InteractionKind::Capture);
  gate.stop();
  gate.stop();
  EXPECT_TRUE(gate.busy());
  EXPECT_FALSE(gate.acquire(InteractionKind::SaveDialog, &capture));
  capture.reset();
  EXPECT_FALSE(gate.busy());
  EXPECT_FALSE(gate.acquire(InteractionKind::Capture));
}
TEST(InteractionGateTest, PinSaveModalReentryIsBusyEvenWithoutWorkflow) {
  InteractionGate gate;
  ResultStore store;
  ExportService exporter;
  PinManager pins;
  const auto id = store.publish(Image{1, 1, {1}});
  ResultActionService* service = nullptr;
  int dialogs = 0;
  ResultActionService actions(store, exporter, pins, [&](HWND) {
    ++dialogs;
    EXPECT_TRUE(gate.busy());
    EXPECT_STREQ(gate.reason(), "save_dialog");
    EXPECT_FALSE(gate.acquire(InteractionKind::Capture));
    EXPECT_EQ(service->save(id).error_code, ErrorCode::kBusy);
    EXPECT_EQ(service->copy(id).error_code, ErrorCode::kBusy);
    EXPECT_EQ(service->pin(id).error_code, ErrorCode::kBusy);
    return std::optional<std::wstring>{};
  }, &gate);
  service = &actions;
  EXPECT_TRUE(actions.savePinImage(Image{1, 1, {2}}).ok);
  EXPECT_EQ(dialogs, 1);
  EXPECT_FALSE(gate.busy());
}
TEST(InteractionGateTest, ShutdownInsideModalRetainsGuardAndSkipsExport) {
  InteractionGate gate;
  ResultStore store;
  ExportService exporter;
  PinManager pins;
  auto capture = gate.acquire(InteractionKind::Capture);
  const auto id = store.publish(Image{1, 1, {1}});
  ResultActionService actions(store, exporter, pins, [&](HWND) {
    gate.stop();
    capture.reset();
    store.clearAll();
    EXPECT_TRUE(gate.busy());
    return std::optional<std::wstring>{L"must-not-be-written.png"};
  }, &gate);
  EXPECT_EQ(actions.save(kGuiResultScopeId, ResultSelection::specific(id), &capture).error_code,
            ErrorCode::kShuttingDown);
  EXPECT_FALSE(gate.busy());
}
TEST(InteractionGateTest, LongShotRejectsIndependentCopyAndPin) {
  InteractionGate gate;
  ResultStore store;
  ExportService exporter;
  PinManager pins;
  ResultActionService actions(store, exporter, pins, {}, &gate);
  auto longshot = gate.acquire(InteractionKind::LongShot);
  EXPECT_EQ(actions.copy(1).error_code, ErrorCode::kBusy);
  EXPECT_EQ(actions.pin(1).error_code, ErrorCode::kBusy);
}
TEST(InteractionGateTest, ForeignThreadCannotReadOrAcquire) {
  InteractionGate gate;
  std::thread worker([&] {
    EXPECT_THROW(gate.busy(), std::logic_error);
    EXPECT_THROW(gate.acquire(InteractionKind::Capture), std::logic_error);
  });
  worker.join();
}
}  // namespace qingying
