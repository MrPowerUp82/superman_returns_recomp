#include "coop_split.h"
#include "coop_lua.h"
#include "coop_second_camera.h"
#include "benchmark_input_driver.h"

#include <memory>
#include <rex/cvar.h>
#include <rex/input/device_assignment.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>

#include "title_benchmark.h"

REXCVAR_DEFINE_BOOL(sr_coop_input, false, "Superman Returns",
                    "Prepare separate controller slots in one instance; second player "
                    "gameplay and split-screen rendering are not implemented")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace superman_returns::coop {

void Install(rex::Runtime& runtime) {
  if (benchmark::Active()) return;
  InstallLuaBridge(runtime);
  if (!REXCVAR_GET(sr_coop_input)) return;
  InstallSecondCamera(runtime);

  if (auto* system = static_cast<rex::input::InputSystem*>(runtime.input_system())) {
    // SDK slot assignment keeps each physical pad on its own guest port,
    // including across disconnects. Keyboard/mouse emulation feeds only port 0.
    system->SetDeviceAssignment(MakeCoopDeviceAssignment());
    REXLOG_INFO("coop: single-instance input preparation; controller ordinals map "
                "to guest ports, keyboard/mouse to port 0");
    REXLOG_WARN("coop: second Superman, independent cameras and split-screen "
                "rendering are not implemented; gameplay is still single-player");
  }
}

}  // namespace superman_returns::coop
