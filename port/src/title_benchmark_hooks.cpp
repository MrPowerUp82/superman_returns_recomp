// Title benchmark: the attract demo video plays through the FMV player while
// the 3D title scene keeps rendering underneath, so its start ends the run.
// sub_8235F2D0 is the player entry below the boot sequence (sub_822B7F38).
#include <rex/hook.h>

#include "generated/default/superman_returns_init.h"
#include "title_benchmark.h"

REX_HOOK_RAW(sub_8235F2D0) {
  superman_returns::benchmark::NoteVideoStarted();
  __imp__sub_8235F2D0(ctx, base);
}
