#include "../../port/src/coop_camera_input_policy.h"
#include "test_main.h"
using namespace superman_returns::coop;
SR_TEST(coop_camera_input_isolated_by_port_actor_and_native_owner) {
  const CameraInputBinding b{0x1000, 0x2000, 1, 42, 3};
  SR_CHECK(CameraForInput(b, 7, 0x1000, 1, 42) == 0x2000);
  SR_CHECK(CameraForInput(b, 7, 0x1000, 0, 1) == 0x1000);
  SR_CHECK(CameraForInput(b, 7, 0x1000, 1, 1) == 0x1000);
  SR_CHECK(CameraForInput(b, 7, 0x1000, 2, 42) == 0x1000);
  SR_CHECK(CameraForInput(b, 7, 0x3000, 1, 42) == 0x3000);
}
SR_TEST(coop_camera_input_rejects_disabled_stale_or_missing_bindings) {
  const CameraInputBinding b{0x1000, 0x2000, 1, 42, 3};
  for (auto session : {0u, 6u, 9u, 11u})
    SR_CHECK(CameraForInput(b, session, 0x1000, 1, 42) == 0x1000);
  for (auto bad : {CameraInputBinding{}, CameraInputBinding{0x1000, 0, 1, 42, 3},
                   CameraInputBinding{0x1000, 0x2000, 42, 42, 3},
                   CameraInputBinding{0x1000, 0x2000, 1, 255, 3}})
    SR_CHECK(CameraForInput(bad, 7, 0x1000, 1, 42) == 0x1000);
}
SR_TEST(coop_camera_modes_use_native_resource_order_and_reject_other_states) {
  SR_CHECK(CameraModeForLocomotion(0x00464C59) == 0);
  SR_CHECK(CameraModeForLocomotion(0x4C4F434F) == 1);
  SR_CHECK(!CameraModeForLocomotion(0));
  SR_CHECK(!CameraModeForLocomotion(0x43555453));
}
SR_TEST(coop_actor_update_exposes_p2_camera_only_to_live_ready_p2) {
  const CameraInputBinding b{0x1000, 0x2000, 1, 42, 3};
  SR_CHECK(CameraForActorUpdate(b, 7, 0x1000, 42, true) == 0x2000);
  SR_CHECK(CameraForActorUpdate(b, 7, 0x1000, 1, true) == 0);    // P1
  SR_CHECK(CameraForActorUpdate(b, 7, 0x1000, 77, true) == 0);   // enemy/NPC
  SR_CHECK(CameraForActorUpdate(b, 7, 0x1000, 42, false) == 0);  // no aim point yet
  SR_CHECK(CameraForActorUpdate(b, 7, 0x2000, 42, true) == 0);   // already swapped / nested
  SR_CHECK(CameraForActorUpdate(b, 7, 0x3000, 42, true) == 0);   // singleton replaced
  SR_CHECK(CameraForActorUpdate(b, 9, 0x1000, 42, true) == 0);   // stale generation
  SR_CHECK(CameraForActorUpdate(b, 6, 0x1000, 42, true) == 0);   // co-op off
  SR_CHECK(CameraForActorUpdate(CameraInputBinding{}, 7, 0x1000, 42, true) == 0);
  SR_CHECK(CameraForActorUpdate(b, 7, 0x1000, 255, true) == 0);
}
SR_TEST(coop_locomotion_camera_never_restarts_native_transitions) {
  SR_CHECK(ShouldForceLocomotionCamera(1, 0, 0));   // landed, still flight camera
  SR_CHECK(ShouldForceLocomotionCamera(0, 1, 0));   // took off, still on-foot camera
  SR_CHECK(!ShouldForceLocomotionCamera(1, 1, 0));  // already matching
  SR_CHECK(!ShouldForceLocomotionCamera(1, 8, 0));  // native transition mode
  SR_CHECK(!ShouldForceLocomotionCamera(1, 8, 3));  // transition in progress
  SR_CHECK(!ShouldForceLocomotionCamera(1, 0, 3));  // state active: would clear +4904
  SR_CHECK(!ShouldForceLocomotionCamera(std::nullopt, 0, 0));
}
