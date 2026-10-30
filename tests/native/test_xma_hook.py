"""Run the actual starvation hook with synchronous runtime-boundary shims.

This checks the post-flush kick's ABI argument and retry behavior, not decoding.
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
#include <cstdint>
#include <cstring>
#include "tests/native/test_main.h"
#include "port/src/xma_flush_policy.h"
union PPCRegister { uint64_t u64 = 0; uint32_t u32; };
struct PPCContext { PPCRegister r3; };
constexpr uint32_t stream = 0x10000, container = 0x10004;
constexpr uint32_t context_address = 0x10008, sound = 0x10050;
std::array<uint8_t, 0x10100> memory_bytes{};
struct Memory { uint8_t* virtual_membase() { return memory_bytes.data(); } };
struct Kernel { Memory* memory() { static Memory m; return &m; } };
namespace rex::system { Kernel* kernel_state() { static Kernel k; return &k; } }
void Store32(uint32_t address, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(memory_bytes.data() + address, &value, 4);
}
bool IsValidGuestPtr(uint32_t p) { return p >= 0x10000 && p <= 0xFFFF0000 - 4; }
uint32_t Load32(const uint8_t* base, uint32_t p) {
  uint32_t value;
  std::memcpy(&value, base + p, 4);
  return __builtin_bswap32(value);
}
struct ContextState { uint32_t dword0, dword1, dword2, dword9; };
ContextState ReadState(const uint8_t* base, uint32_t p) {
  return {Load32(base, p), 1, 0, 0};
}
struct XmaContext {};
XmaContext* FindContext(uint32_t p) {
  static XmaContext c;
  return p == context_address ? &c : nullptr;
}
bool IsDeadContext(const uint8_t*, uint32_t, const ContextState&, XmaContext* c) {
  return c == nullptr;
}
// A live, valid output buffer: the hook need not repair output validity here.
struct XMA_CONTEXT_DATA {
  bool output_buffer_valid = true;
  uint32_t output_buffer_ptr = 0x10080, output_buffer_block_count = 1;
  explicit XMA_CONTEXT_DATA(const uint8_t*) {}
  void Store(uint8_t*) const {}
};
void LogGiveUp(const char*, uint32_t, uint32_t, const ContextState&) {}
struct MixerCall { uint32_t sound; int32_t requested; } g_mixer_call{sound, 256};
bool carry_queued = false, carry_available = true;
uint32_t kicks = 0, flushes = 0;
std::array<uint32_t, 3> kick_addresses{};
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
SR_TEST(xma_hook_rekicks_the_original_context_after_the_export_clobbers_r3) {
  Store32(stream, container);
  Store32(container, context_address);
  Store32(sound + 36, 256);
  PPCRegister waiting, current_stream;
  waiting.u64 = 1;
  current_stream.u64 = stream;
  XmaKickStarvedContext(waiting, current_stream);
  SR_CHECK_EQ(kicks, 2u);
  SR_CHECK_EQ(kick_addresses[0], context_address);
  SR_CHECK_EQ(kick_addresses[1], context_address);
  SR_CHECK_EQ(Load32(memory_bytes.data(), context_address), 1u);
  SR_CHECK_EQ(waiting.u32, 1u);  // Retry the mixer to consume flushed output.
  // Once the carry is gone, a subsequent starved pass must give up, not spin.
  waiting.u64 = 1;
  XmaKickStarvedContext(waiting, current_stream);
  SR_CHECK_EQ(kicks, 3u);
  SR_CHECK_EQ(kick_addresses[2], context_address);
  SR_CHECK_EQ(flushes, 1u);
  SR_CHECK_EQ(waiting.u32, 0u);
}
int main() { return sr_test::RunAll(); }
'''


class XmaHookRegression(unittest.TestCase):
    def test_post_flush_kick_keeps_context_argument(self):
        compiler = os.environ.get("CXX") or shutil.which("clang++")
        if not compiler:
            bundled = sorted((ROOT / ".tools").glob("clang+llvm-*/bin/clang++.exe"))
            compiler = str(bundled[-1]) if bundled else None
        self.assertIsNotNone(compiler, "Set CXX to a C++20 compiler")
        source = (ROOT / "port/src/xma_fixes.cpp").read_text(encoding="utf-8")
        # The hook is the final function; compile its current body verbatim.
        hook = source[source.index("void XmaKickStarvedContext("):]
        with tempfile.TemporaryDirectory(prefix="xma-hook-regression-", dir=ROOT / ".tools") as tmp:
            source_path = Path(tmp) / "hook.cpp"
            executable = Path(tmp) / "xma_hook.exe"
            source_path.write_text(FIXTURE + hook + CHECKS, encoding="utf-8")
            build = subprocess.run(
                [compiler, "-std=c++20", "-I", str(ROOT), str(source_path), "-o", str(executable)],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
