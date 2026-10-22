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
//  * The context has run out of input (a streamed sound, such as Jor-El's
//    voice in the intro, waiting for its next disc read) or its output buffer
//    is full or flagged invalid ("full") while the game's read offset update
//    races the decoder. -> stop waiting as soon as a kick makes no progress.
//
// Stopping the wait (r26 = 0) makes the mixer move on to the next stream, so
// at worst one sound skips part of a mix pass. Before this fix the audio
// thread spun forever, stopped flushing its 1 MB command queue
// (sub_8264C540), and the queue overflowed into the heap, crashing a minute
// or more into 3D scenes (calls to 0xC003000C, writes to 0x8264CE28).
//
// The runtime's XMAEnableContext decodes inline (XmaDecoder::WriteRegister
// runs XmaContext::Work on the calling thread), so when the kick returns the
// context already holds everything it can decode. If the context did not
// change, nothing will arrive while the mixer keeps spinning: the data it
// needs comes from guest threads that run after the mix pass. An earlier
// version waited up to 4 ms per starved stream on every mix pass; with a
// streamed voice split over several contexts that added up to more than one
// audio frame per pass, the mixer fell behind real time and only the sound
// stuttered while the game kept running normally.
//
// Mid-asm hook at 0x826595B8 (`cmplwi cr6,r26,0` before the retry branch);
// r31 points at the stream, whose first word points at {xma_context*, ...}.
//
// The runtime decoder also never emits a stream's last block: it delays each
// block by one frame (the tail waits in a carry buffer for the next block) and
// has no flush when the input runs out, so the final kDecoderStartPadding-
// shifted frame (one full 512-sample frame) is missing and the mixer waits for
// it. When a kick makes no progress, the stream is provably at its end (no
// input left, the sound's last segment, the mixer asking for everything the
// segment has left) and a decoded block is still carried, the carry is emitted
// followed by silence and the context is kicked once more. See
// xma_flush_policy.h; SR_XMA_FLUSH=0 turns the flush off.
//
// The sound the mixer works on (sub_826592F0's r3) is not visible from the
// mid-asm hook, so an entry hook keeps it, and the sample request (r5), in
// thread-local state for the duration of the call.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <thread>

#include <rex/audio/audio_system.h>
#include <rex/audio/xma/context.h>
#include <rex/audio/xma/decoder.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>

#include "generated/default/superman_returns_init.h"
#include "xma_flush_policy.h"

namespace {

using rex::audio::XMA_CONTEXT_DATA;
using rex::audio::XmaContext;
using rex::audio::XmaDecoder;

// The decoder's carry state is private. These keys name the members through
// explicit instantiation, which the language allows to ignore access control;
// a changed SDK layout fails to compile instead of corrupting memory.
template <typename Tag, typename Tag::Type Member>
struct Key {
  friend typename Tag::Type Unlock(Tag) { return Member; }
};
using CarryBlock = std::array<uint8_t, XmaContext::kBytesPerFrameChannel * 2>;
struct LockTag {
  using Type = std::mutex XmaContext::*;
  friend Type Unlock(LockTag);
};
struct CarryFrameTag {
  using Type = CarryBlock XmaContext::*;
  friend Type Unlock(CarryFrameTag);
};
struct RawFrameTag {
  using Type = CarryBlock XmaContext::*;
  friend Type Unlock(RawFrameTag);
};
struct RemainingSubframesTag {
  using Type = uint8_t XmaContext::*;
  friend Type Unlock(RemainingSubframesTag);
};
struct CarryValidTag {
  using Type = bool XmaContext::*;
  friend Type Unlock(CarryValidTag);
};
struct PendingLimitTag {
  using Type = uint8_t XmaContext::*;
  friend Type Unlock(PendingLimitTag);
};
struct PendingSkipTag {
  using Type = uint8_t XmaContext::*;
  friend Type Unlock(PendingSkipTag);
};
struct LoopLimitTag {
  using Type = uint8_t XmaContext::*;
  friend Type Unlock(LoopLimitTag);
};
struct ContextsTag {
  using Type = XmaContext (XmaDecoder::*)[320];
  friend Type Unlock(ContextsTag);
};
template struct Key<LockTag, &XmaContext::lock_>;
template struct Key<CarryFrameTag, &XmaContext::carry_frame_>;
template struct Key<RawFrameTag, &XmaContext::raw_frame_>;
template struct Key<RemainingSubframesTag, &XmaContext::current_frame_remaining_subframes_>;
template struct Key<CarryValidTag, &XmaContext::carry_valid_>;
template struct Key<PendingLimitTag, &XmaContext::pending_output_limit_>;
template struct Key<PendingSkipTag, &XmaContext::pending_start_skip_>;
template struct Key<LoopLimitTag, &XmaContext::loop_frame_output_limit_>;
template struct Key<ContextsTag, &XmaDecoder::contexts_>;

using Clock = std::chrono::steady_clock;

// Safety net for a context that keeps making progress without ever giving
// the mixer enough samples. Well under one 256-sample audio frame (5.3 ms).
constexpr auto kMaxStarvedWait = std::chrono::milliseconds(1);

uint32_t Load32(const uint8_t* base, uint32_t addr) {
  return __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + addr));
}

// The context words the decoder updates: input buffer valid bits and output
// write offset (dword 0), output buffer valid (dword 1), input read offset
// (dword 2) and output read offset (dword 9).
struct ContextState {
  uint32_t dword0;
  uint32_t dword1;
  uint32_t dword2;
  uint32_t dword9;

  bool operator==(const ContextState&) const = default;
};

ContextState ReadState(const uint8_t* base, uint32_t xma_context) {
  return {Load32(base, xma_context), Load32(base, xma_context + 4),
          Load32(base, xma_context + 8), Load32(base, xma_context + 36)};
}

// Nothing to decode and nothing to read: input buffers 0/1 and the output
// buffer are all invalid (a released context is fully zeroed).
bool IsDeadContext(const ContextState& state) {
  const bool input_valid = (state.dword0 >> 20) & 3;
  const bool output_valid = state.dword1 >> 31;
  return !input_valid && !output_valid;
}

void LogGiveUp(const char* reason, uint32_t xma_context, uint32_t stream) {
  static std::atomic<uint32_t> count{0};
  if (count.fetch_add(1) < 10) {
    REXAPU_INFO("XMA fix: stopped waiting on context {:08X} (stream {:08X}): {}", xma_context,
                stream, reason);
  }
}

// The sound the mixer is working on and the samples its caller asked for;
// set by the sub_826592F0 entry hook, read by the mid-asm hook on one thread.
struct MixerCall {
  uint32_t sound = 0;
  int32_t requested = 0;
};
thread_local MixerCall g_mixer_call;

std::atomic<uint32_t> g_flushed{0};
std::atomic<uint32_t> g_declined{0};
// Indexed by XmaFlushDecision; slot 0 (Flush) stays unused.
std::array<std::atomic<uint32_t>, 8> g_declined_by_reason{};

bool FlushEnabled() {
  static const bool enabled = [] {
    const char* v = std::getenv("SR_XMA_FLUSH");
    return !(v && v[0] == '0');
  }();
  return enabled;
}

XmaContext* FindContext(uint32_t guest_ptr) {
  auto* audio = static_cast<rex::audio::AudioSystem*>(
      rex::system::kernel_state()->emulator()->audio_system());
  XmaDecoder* decoder = audio ? audio->xma_decoder() : nullptr;
  if (!decoder) return nullptr;
  XmaContext* contexts = decoder->*Unlock(ContextsTag{});
  for (uint32_t i = 0; i < 320; ++i) {
    if (contexts[i].guest_ptr() == guest_ptr && contexts[i].is_allocated()) return &contexts[i];
  }
  return nullptr;
}

// Emit the carried tail of the last decoded block, then silence, as one more
// output frame. Returns true when the carry was queued for the next kick.
bool TryFlushFinalBlock(const uint8_t* base, uint32_t xma_context, uint32_t stream) {
  if (!FlushEnabled()) return false;

  const uint32_t sound = g_mixer_call.sound;
  const uint32_t remaining = sound ? Load32(base, sound + 36) : 0;
  const uint32_t head = sound ? Load32(base, sound + 48) : 0;
  const uint32_t next = head ? Load32(base, head) : 0;

  superman_returns::XmaFlushInputs in;
  in.final_request = superman_returns::IsFinalXmaRequest(remaining, g_mixer_call.requested);
  in.last_segment = superman_returns::IsLastXmaSegment(head, next);

  XmaContext* context = FindContext(xma_context);
  in.context_found = context != nullptr;
  XMA_CONTEXT_DATA data(base + xma_context);
  in.input_exhausted = !data.IsAnyInputBufferValid();
  in.output_valid = data.output_buffer_valid;

  std::unique_lock<std::mutex> lock;
  if (context) {
    lock = std::unique_lock<std::mutex>(context->*Unlock(LockTag{}));
    in.carry_valid = context->*Unlock(CarryValidTag{});
    in.block_pending = (context->*Unlock(RemainingSubframesTag{})) != 0;
  }

  const auto decision = superman_returns::DecideXmaFinalFlush(in);
  if (decision != superman_returns::XmaFlushDecision::Flush) {
    const uint32_t n = g_declined.fetch_add(1) + 1;
    g_declined_by_reason[static_cast<size_t>(decision)].fetch_add(1);
    // The first decline of each reason, then a summary every 100 declines.
    if (g_declined_by_reason[static_cast<size_t>(decision)].load() == 1) {
      REXAPU_INFO(
          "XMA fix: no final-block flush for context {:08X} (stream {:08X}, sound {:08X}): {}",
          xma_context, stream, sound, superman_returns::XmaFlushDecisionName(decision));
    }
    if (n % 100 == 0) {
      std::string summary;
      for (size_t i = 1; i < g_declined_by_reason.size(); ++i) {
        const uint32_t count = g_declined_by_reason[i].load();
        if (!count) continue;
        summary += std::format(" {}={}", superman_returns::XmaFlushDecisionName(
                                             static_cast<superman_returns::XmaFlushDecision>(i)),
                               count);
      }
      REXAPU_INFO("XMA fix: {} flushes, {} declined:{}", g_flushed.load(), n, summary);
    }
    return false;
  }

  // Same block layout as XmaContext::Decode: the carried tail, then the head
  // of the next block, which does not exist, so silence.
  const uint32_t stereo = data.is_stereo ? 1 : 0;
  const size_t frame_bytes = size_t(XmaContext::kBytesPerFrameChannel) << stereo;
  const size_t pad_bytes =
      (size_t(XmaContext::kDecoderStartPadding) * XmaContext::kBytesPerSample) << stereo;
  const size_t carry_bytes = frame_bytes - pad_bytes;
  auto& raw = context->*Unlock(RawFrameTag{});
  const auto& carry = context->*Unlock(CarryFrameTag{});
  std::memcpy(raw.data(), carry.data(), carry_bytes);
  std::memset(raw.data() + carry_bytes, 0, raw.size() - carry_bytes);

  const uint8_t subframes = uint8_t(4u << stereo);
  const uint8_t skip = std::min<uint8_t>(context->*Unlock(PendingSkipTag{}), subframes);
  context->*Unlock(RemainingSubframesTag{}) = uint8_t(subframes - skip);
  context->*Unlock(LoopLimitTag{}) = context->*Unlock(PendingLimitTag{});
  context->*Unlock(PendingLimitTag{}) = 0;
  context->*Unlock(PendingSkipTag{}) = 0;
  context->*Unlock(CarryValidTag{}) = false;

  const uint32_t n = g_flushed.fetch_add(1) + 1;
  if (n <= 50 || n % 100 == 0) {
    REXAPU_INFO(
        "XMA fix: flushed final block of context {:08X} (stream {:08X}, sound {:08X}, "
        "{} samples left, {} requested, {}) [flush #{}, {} declined]",
        xma_context, stream, sound, remaining, g_mixer_call.requested,
        stereo ? "stereo" : "mono", n, g_declined.load());
  }
  return true;
}

}  // namespace

// sub_826592F0 is the audio mixer; remember the sound and the request for the
// mid-asm hook below.
REX_HOOK_RAW(sub_826592F0) {
  const MixerCall saved = g_mixer_call;
  g_mixer_call = {ctx.r3.u32, static_cast<int32_t>(ctx.r5.u32)};
  __imp__sub_826592F0(ctx, base);
  g_mixer_call = saved;
}

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

  auto give_up = [&](const char* reason) {
    LogGiveUp(reason, xma_context, stream);
    waiting_stream = 0;
    r26.u64 = 0;
  };

  if (!xma_context) {
    give_up("released");
    return;
  }
  const ContextState before = ReadState(base, xma_context);
  if (IsDeadContext(before)) {
    give_up("released");
    return;
  }

  const auto now = Clock::now();
  if (stream != waiting_stream) {
    waiting_stream = stream;
    waiting_since = now;
  } else if (now - waiting_since > kMaxStarvedWait) {
    give_up("timed out");
    return;
  }

  PPCContext call_ctx{};
  call_ctx.r3.u64 = xma_context;
  __imp__XMAEnableContext(call_ctx, base);

  // The kick decoded synchronously. No change means no input left or no room
  // in the output buffer; spinning cannot fix either.
  if (ReadState(base, xma_context) == before) {
    // The stream may simply be over: hand the mixer the block the decoder held
    // back and kick once more.
    if (TryFlushFinalBlock(base, xma_context, stream)) {
      __imp__XMAEnableContext(call_ctx, base);
      if (ReadState(base, xma_context) != before) {
        return;
      }
    }
    give_up("no progress");
  }
}
