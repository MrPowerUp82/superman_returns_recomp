// Ported from crazyriddler/rexglue-native-kit @136bc6c4,
// reference/conan/port/src/native/hang_watchdog.h (Conan native renderer).
// The kit ships no license file at that revision; parts derived from Xenia /
// ReXGlue keep their BSD license. Changes for Superman Returns are listed in
// docs/native-port-plan.md section 2.
//
#pragma once

namespace superman_returns::native {

// Called once per guest swap; starts the watchdog thread on first use.
void HangWatchdogBeat();

}  // namespace superman_returns::native
