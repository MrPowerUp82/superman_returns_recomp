// XMA starvation fix for the audio mixer (sub_826592F0).
//
// The mixer busy-waits until each XMA context's output write offset gives it
// enough samples, retrying the same stream while r26 != 0 with no timeout.
// Under the runtime's XMA emulation some contexts never deliver:
//
//  * The runtime decodes one pass per kick and then disables the context,
//    while real hardware keeps decoding. A pass that produced too few samples
//    is never followed by another one. -> kick the context again.
//  * The stream's sound has finished and its context was already released
//    (the whole context block is zeroed), but the stream is still in the mix
//    list. A released context can never produce output. -> stop waiting.
//  * The output buffer gets flagged invalid ("full") while the game's read
//    offset update races the decoder, and the state never recovers.
//    -> stop waiting after a short timeout; the next mix pass retries.
//
// Stopping the wait (r26 = 0) makes the mixer move on to the next stream, so
// at worst one sound skips part of a mix pass. Before this fix the audio
// thread spun forever, stopped flushing its 1 MB command queue
// (sub_8264C540), and the queue overflowed into the heap, crashing a minute
// or more into 3D scenes (calls to 0xC003000C, writes to 0x8264CE28).
//
// Mid-asm hook at 0x826595B8 (`cmplwi cr6,r26,0` before the retry branch);
// r31 points at the stream, whose first word points at {xma_context*, ...}.

#include <atomic>
#include <chrono>
#include <thread>

#include <rex/hook.h>
#include <rex/logging.h>

#include "generated/default/superman_returns_init.h"

namespace {

using Clock = std::chrono::steady_clock;

// About one audio frame; real XMA hardware delivers well within this.
constexpr auto kMaxStarvedWait = std::chrono::milliseconds(4);

uint32_t Load32(const uint8_t* base, uint32_t addr) {
  return __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + addr));
}

// Nothing to decode and nothing to read: input buffers 0/1 and the output
// buffer are all invalid (a released context is fully zeroed).
bool IsDeadContext(const uint8_t* base, uint32_t xma_context) {
  const uint32_t dword0 = Load32(base, xma_context);
  const uint32_t dword1 = Load32(base, xma_context + 4);
  const bool input_valid = (dword0 >> 20) & 3;
  const bool output_valid = dword1 >> 31;
  return !input_valid && !output_valid;
}

void LogGiveUp(const char* reason, uint32_t xma_context, uint32_t stream) {
  static std::atomic<uint32_t> count{0};
  if (count.fetch_add(1) < 10) {
    REXAPU_INFO("XMA fix: stopped waiting on context {:08X} (stream {:08X}): {}", xma_context,
                stream, reason);
  }
}

}  // namespace

void XmaKickStarvedContext(PPCRegister& r26, PPCRegister& r31) {
  // The audio mixer runs on a single guest thread.
  static thread_local uint32_t waiting_stream = 0;
  static thread_local Clock::time_point waiting_since;

  if (r26.u32 == 0) {  // Enough samples; the mixer is not waiting.
    waiting_stream = 0;
    return;
  }

  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  const uint32_t stream = r31.u32;
  const uint32_t xma_context = Load32(base, Load32(base, stream));

  if (!xma_context || IsDeadContext(base, xma_context)) {
    LogGiveUp("released", xma_context, stream);
    waiting_stream = 0;
    r26.u64 = 0;
    return;
  }

  const auto now = Clock::now();
  if (stream != waiting_stream) {
    waiting_stream = stream;
    waiting_since = now;
  } else if (now - waiting_since > kMaxStarvedWait) {
    LogGiveUp("timed out", xma_context, stream);
    waiting_stream = 0;
    r26.u64 = 0;
    return;
  }

  PPCContext call_ctx{};
  call_ctx.r3.u64 = xma_context;
  __imp__XMAEnableContext(call_ctx, base);
  std::this_thread::yield();
}
