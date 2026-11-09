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
