"""Run the actual starvation hook with synchronous runtime-boundary shims.

This checks guest address translation, the post-flush kick's ABI argument and the
retry / silence / give-up decisions, not decoding.
Run directly with Python; only a C++20 compiler is needed, no game/runtime link.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

FIXTURE = r'''
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include "tests/native/test_main.h"
#include "port/src/xma_flush_policy.h"
union PPCRegister { uint64_t u64 = 0; uint32_t u32; };
struct PPCContext { PPCRegister r3; };
// Every guest object sits in the 0xE0000000 heap, as the game's XMA contexts
// do. The host maps that heap 0x1000 bytes higher, so `base + address` reads
// zeroes here and any access that skips the translation fails the checks.
constexpr uint32_t kHeap = 0xE0000000;
constexpr uint32_t stream = kHeap + 0x100, container = kHeap + 0x140;
constexpr uint32_t context_address = kHeap + 0x180, sound = kHeap + 0x200;
std::array<uint8_t, 0x2000> memory_bytes{};
uint8_t* FakeBase() {
  return reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(memory_bytes.data()) - kHeap);
}
namespace rex::memory {
uint32_t translations = 0;
inline uint8_t* GuestPtr(uint8_t* base, uint32_t guest_address) {
  ++translations;
  return base + guest_address + (guest_address >= kHeap ? 0x1000u : 0u);
}
}  // namespace rex::memory
struct Memory { uint8_t* virtual_membase() { return FakeBase(); } };
struct Kernel { Memory* memory() { static Memory m; return &m; } };
namespace rex::system { Kernel* kernel_state() { static Kernel k; return &k; } }
void Store32(uint32_t address, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(rex::memory::GuestPtr(FakeBase(), address), &value, 4);
}
using Clock = std::chrono::steady_clock;
constexpr auto kMaxStarvedWait = std::chrono::milliseconds(1);
'''

# Shims for everything else the hook body calls.
SHIMS = r'''
struct XmaContext {};
XmaContext* FindContext(uint32_t p) {
  static XmaContext c;
  return p == context_address ? &c : nullptr;
}
bool IsDeadContext(const uint8_t*, uint32_t, const ContextState&, XmaContext* c) {
  return c == nullptr;
}
struct HookStats { uint64_t starved = 0, kicks = 0, progress = 0; } g_stats;
void MaybeLogStats() {}
const char* last_give_up = "";
void LogGiveUp(const char* reason, uint32_t, uint32_t, const ContextState&) {
  last_give_up = reason;
}
struct MixerCall { uint32_t sound; int32_t requested; } g_mixer_call{sound, 256};
bool carry_queued = false, carry_available = true, ring_has_room = true;
uint32_t kicks = 0, flushes = 0, rearms = 0, pads = 0;
std::array<uint32_t, 8> kick_addresses{};
bool RearmEmptyOutput(const uint8_t*, uint32_t) { ++rearms; return false; }
bool PadOutputWithSilence(const uint8_t*, uint32_t p) {
  if (p != context_address || !ring_has_room) return false;
  ++pads;
  return true;
}
bool TryFlushFinalBlock(const uint8_t*, uint32_t, uint32_t) {
  if (!carry_available) return false;
  carry_available = false;
  carry_queued = true;
  ++flushes;
  return true;
}
void __imp__XMAEnableContext(PPCContext& ctx, uint8_t*) {
  if (kicks < kick_addresses.size()) kick_addresses[kicks] = ctx.r3.u32;
  ++kicks;
  if (ctx.r3.u32 == context_address && carry_queued) {
    Store32(context_address, 1);  // Carry output advanced the write offset.
    carry_queued = false;
  }
  ctx.r3.u64 = 0;  // XMAEnableContext returns zero through the PPC ABI.
}
'''

CHECKS = r'''
SR_TEST(xma_hook_reaches_contexts_in_the_e0_heap_and_never_skips_a_live_stream) {
  Store32(stream, container);
  Store32(container, context_address);
  Store32(sound + 36, 256);
  PPCRegister waiting, current_stream;
  waiting.u64 = 1;
  current_stream.u64 = stream;

  // End of the sound: the flush queues the carry and the second kick emits it.
  XmaKickStarvedContext(waiting, current_stream);
  SR_CHECK(rex::memory::translations > 0);
  SR_CHECK_EQ(kicks, 2u);
  SR_CHECK_EQ(kick_addresses[0], context_address);
  SR_CHECK_EQ(kick_addresses[1], context_address);  // r3 restored after the export
  SR_CHECK_EQ(Load32(FakeBase(), context_address), 1u);
  SR_CHECK_EQ(waiting.u32, 1u);  // Retry the mixer to consume flushed output.
  SR_CHECK_EQ(pads, 0u);

  // No carry left and the kick changes nothing: silence, and the mixer retries.
  waiting.u64 = 1;
  XmaKickStarvedContext(waiting, current_stream);
  SR_CHECK_EQ(kicks, 3u);
  SR_CHECK_EQ(flushes, 1u);
  SR_CHECK_EQ(pads, 1u);
  SR_CHECK_EQ(waiting.u32, 1u);  // r26 = 0 here would desynchronise the sound.

  // Only a ring with no room at all makes the hook let the stream go.
  ring_has_room = false;
  XmaKickStarvedContext(waiting, current_stream);
  SR_CHECK_EQ(waiting.u32, 0u);
  SR_CHECK_EQ(std::strcmp(last_give_up, "no progress"), 0);

  // Nothing queued: the mixer asks for zero samples; skip without a kick.
  ring_has_room = true;
  Store32(sound + 36, 0);
  const uint32_t kicks_before = kicks;
  waiting.u64 = 1;
  XmaKickStarvedContext(waiting, current_stream);
  SR_CHECK_EQ(waiting.u32, 0u);
  SR_CHECK_EQ(kicks, kicks_before);
  SR_CHECK_EQ(pads, 1u);
  SR_CHECK_EQ(std::strcmp(last_give_up, "nothing queued"), 0);
}
int main() { return sr_test::RunAll(); }
'''


class XmaHookRegression(unittest.TestCase):
    def test_hook_body_against_runtime_boundary_shims(self):
        compiler = os.environ.get("CXX") or shutil.which("clang++")
        if not compiler:
            bundled = sorted((ROOT / ".tools").glob("clang+llvm-*/bin/clang++.exe"))
            compiler = str(bundled[-1]) if bundled else None
        self.assertIsNotNone(compiler, "Set CXX to a C++20 compiler")
        source = (ROOT / "port/src/xma_fixes.cpp").read_text(encoding="utf-8")
        # Compile the guest-memory helpers and the hook (the final function)
        # verbatim, so the address translation under test is the shipped one.
        access = source[source.index("inline bool IsValidGuestPtr("):source.index("XmaContext* FindContext(")]
        hook = source[source.index("void XmaKickStarvedContext("):]
        with tempfile.TemporaryDirectory(prefix="xma-hook-regression-", dir=ROOT / ".tools") as tmp:
            source_path = Path(tmp) / "hook.cpp"
            executable = Path(tmp) / "xma_hook.exe"
            source_path.write_text(FIXTURE + access + SHIMS + hook + CHECKS, encoding="utf-8")
            build = subprocess.run(
                [compiler, "-std=c++20", "-I", str(ROOT), str(source_path), "-o", str(executable)],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
