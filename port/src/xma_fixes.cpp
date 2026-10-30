// XMA starvation fix for the audio mixer (sub_826592F0).
//
// The mixer busy-waits until each XMA context's output write offset gives it
// enough samples, retrying the same stream while r26 != 0.
// Under ReXGlue's XMA emulation:
//
//  1. ReXGlue decodes synchronously inline during XMAEnableContext, rather than
//     asynchronously like Xbox 360 hardware. If a kick makes no progress, spinning
//     or yielding cannot produce new data: the input packet feeder (sub_826586A0)
//     runs AFTER the stream mix loop (loc_82659658). Spinning in the stream loop
//     deadlocks the audio thread and blocks the feeder from replenishing packets.
//     Therefore, when a kick yields no progress, r26 MUST be set to 0 to exit the
//     loop and let sub_826586A0 execute.
//
//  2. In ReXGlue (xma_context.cpp line 146), when the output ring buffer is empty
//     (output_rb.empty()), ReXGlue clears data.output_buffer_valid = 0.
//     On subsequent kicks, ReXGlue checks "if (!data.output_buffer_valid) return true;"
//     and refuses to decode any newly fed packets into the buffer.
//     Furthermore, Xbox 360 hardware uses output_buffer_valid == 0 to signal that
//     the buffer is FULL; when empty, the game expects output_buffer_valid == 1.
//     ReXGlue's inverted behavior caused the game to misinterpret empty buffers as
//     full (adding 6144 phantom samples) or to deadlock with no decodes.
//     Fix: We ensure output_buffer_valid = 1 whenever a context has an active
//     output buffer, both before and after feeder/mixer passes.
//
//  3. The runtime decoder delays each block by one frame (the tail waits in a
//     carry buffer for the next block) and has no flush when input runs out, so
//     the final frame is missing and the mixer waits for it. TryFlushFinalBlock emits
//     the carry tail followed by silence when the sound reaches its final request.
//     SR_XMA_FLUSH=0 turns the flush off.
//
// Mid-asm hook at 0x826595B8 (`cmplwi cr6,r26,0` before the retry branch);
// r31 points at the stream, whose first word points at {xma_context*, ...}.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>

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

static_assert(superman_returns::kXmaBitsPerPacket == XmaContext::kBitsPerPacket);

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

inline bool IsValidGuestPtr(uint32_t ptr) {
  return ptr >= 0x00010000 && ptr <= 0xFFFF0000 - 4;
}

uint32_t Load32(const uint8_t* base, uint32_t addr) {
  if (!base || !IsValidGuestPtr(addr)) return 0;
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
  if (!base || !IsValidGuestPtr(xma_context)) return {};
  return {Load32(base, xma_context), Load32(base, xma_context + 4),
          Load32(base, xma_context + 8), Load32(base, xma_context + 36)};
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

// A context is dead if it is not allocated in the runtime, or if all dwords are zeroed,
// or if output buffer has no valid address/size.
bool IsDeadContext(const uint8_t* base, uint32_t xma_context, const ContextState& state,
                   XmaContext* context) {
  if (!context || !IsValidGuestPtr(xma_context)) return true;
  if (state.dword0 == 0 && state.dword1 == 0 && state.dword2 == 0 && state.dword9 == 0) return true;
  XMA_CONTEXT_DATA data(base + xma_context);
  if (data.output_buffer_ptr == 0 || data.output_buffer_block_count == 0) return true;
  return false;
}

void LogGiveUp(const char* reason, uint32_t xma_context, uint32_t stream,
               const ContextState& state) {
  static std::atomic<uint32_t> count{0};
  if (count.fetch_add(1) < 20) {
    REXAPU_INFO(
        "XMA fix: stopped waiting on context {:08X} (stream {:08X}): {} "
        "[dw0={:08X} dw1={:08X} dw2={:08X} dw9={:08X}]",
        xma_context, stream, reason, state.dword0, state.dword1, state.dword2, state.dword9);
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

bool FlushEnabled() {
  static const bool enabled = [] {
    const char* v = std::getenv("SR_XMA_FLUSH");
    return !(v && v[0] == '0');
  }();
  return enabled;
}

// Emit the carried tail of the last decoded block, then silence, as one more
// output frame. Returns true when the carry was queued for the next kick.
bool TryFlushFinalBlock(const uint8_t* base, uint32_t xma_context, uint32_t stream) {
  if (!FlushEnabled()) return false;

  const uint32_t sound = g_mixer_call.sound;
  const uint32_t remaining = IsValidGuestPtr(sound) ? Load32(base, sound + 36) : 0;
  if (!superman_returns::IsFinalXmaRequest(remaining, g_mixer_call.requested)) {
    return false;
  }

  const uint32_t head = IsValidGuestPtr(sound) ? Load32(base, sound + 48) : 0;
  const uint32_t next = IsValidGuestPtr(head) ? Load32(base, head) : 0;

  superman_returns::XmaFlushInputs in;
  in.final_request = true;
  in.last_segment = superman_returns::IsLastXmaSegment(head, next);

  XmaContext* context = FindContext(xma_context);
  in.context_found = context != nullptr;
  XMA_CONTEXT_DATA data(base + xma_context);

  in.input_exhausted = superman_returns::IsXmaInputExhausted(
      data.input_buffer_0_valid != 0, data.input_buffer_1_valid != 0,
      data.current_buffer, data.input_buffer_0_packet_count,
      data.input_buffer_1_packet_count, data.input_buffer_read_offset,
      data.error_status);
  in.output_valid = (data.output_buffer_ptr != 0 && data.output_buffer_block_count > 0);

  std::unique_lock<std::mutex> lock;
  if (context) {
    lock = std::unique_lock<std::mutex>(context->*Unlock(LockTag{}));
    in.carry_valid = context->*Unlock(CarryValidTag{});
    in.block_pending = (context->*Unlock(RemainingSubframesTag{})) != 0;
  }

  const auto decision = superman_returns::DecideXmaFinalFlush(in);
  if (decision != superman_returns::XmaFlushDecision::Flush) {
    const uint32_t n = g_declined.fetch_add(1) + 1;
    if (n <= 10) {
      REXAPU_INFO(
          "XMA fix: no final-block flush for context {:08X} (stream {:08X}, sound {:08X}): {}",
          xma_context, stream, sound, superman_returns::XmaFlushDecisionName(decision));
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

  // Re-enable output_buffer_valid in guest memory so ReXGlue Work() will consume the carry frame
  data.output_buffer_valid = 1;
  data.Store(const_cast<uint8_t*>(base) + xma_context);

  const uint32_t n = g_flushed.fetch_add(1) + 1;
  if (n <= 20) {
    REXAPU_INFO(
        "XMA fix: flushed final block of context {:08X} (stream {:08X}, sound {:08X}, "
        "{} samples left, {} requested, {}) [flush #{}]",
        xma_context, stream, sound, remaining, g_mixer_call.requested,
        stereo ? "stereo" : "mono", n);
  }
  return true;
}

// Ensures all XMA contexts associated with a sound have output_buffer_valid = 1.
// ReXGlue incorrectly clears output_buffer_valid on empty, which prevents decoding
// newly fed input packets and confuses the game into treating empty buffers as full.
void EnsureContextsValid(const uint8_t* base, uint32_t sound) {
  if (!base || !IsValidGuestPtr(sound)) return;
  const uint32_t stream_count = Load32(base, sound + 32);
  const uint32_t stream_array = Load32(base, sound + 24);
  if (!IsValidGuestPtr(stream_array) || stream_count == 0 || stream_count > 64) return;
  for (uint32_t i = 0; i < stream_count; ++i) {
    const uint32_t stream = stream_array + i * 24;
    if (!IsValidGuestPtr(stream)) continue;
    const uint32_t container = Load32(base, stream);
    if (!IsValidGuestPtr(container)) continue;
    const uint32_t xma_context = Load32(base, container);
    if (!IsValidGuestPtr(xma_context)) continue;
    XMA_CONTEXT_DATA data(base + xma_context);
    if (!data.output_buffer_valid && data.output_buffer_ptr != 0 && data.output_buffer_block_count > 0) {
      data.output_buffer_valid = 1;
      data.Store(const_cast<uint8_t*>(base) + xma_context);
    }
  }
}

}  // namespace

// sub_826586A0 supplies input packets to streams of a sound.
// Ensure contexts have valid output buffers so newly supplied packets are decoded.
REX_HOOK_RAW(sub_826586A0) {
  const uint32_t sound = ctx.r3.u32;
  EnsureContextsValid(base, sound);
  __imp__sub_826586A0(ctx, base);
  EnsureContextsValid(base, sound);
}

// sub_826592F0 is the audio mixer; remember the sound and the request for the
// mid-asm hook below.
REX_HOOK_RAW(sub_826592F0) {
  const uint32_t sound = ctx.r3.u32;
  EnsureContextsValid(base, sound);
  const MixerCall saved = g_mixer_call;
  g_mixer_call = {sound, static_cast<int32_t>(ctx.r5.u32)};
  __imp__sub_826592F0(ctx, base);
  g_mixer_call = saved;
  EnsureContextsValid(base, sound);
}

void XmaKickStarvedContext(PPCRegister& r26, PPCRegister& r31) {
  if (r26.u32 == 0) {  // Enough samples; the mixer is not waiting.
    return;
  }

  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  const uint32_t stream = r31.u32;
  if (!IsValidGuestPtr(stream)) {
    r26.u64 = 0;
    return;
  }
  const uint32_t container = Load32(base, stream);
  if (!IsValidGuestPtr(container)) {
    r26.u64 = 0;
    return;
  }
  const uint32_t xma_context = Load32(base, container);
  if (!IsValidGuestPtr(xma_context)) {
    LogGiveUp("released", 0, stream, {});
    r26.u64 = 0;
    return;
  }

  const ContextState before_pre = ReadState(base, xma_context);
  XmaContext* context = FindContext(xma_context);
  if (IsDeadContext(base, xma_context, before_pre, context)) {
    LogGiveUp("released", xma_context, stream, before_pre);
    r26.u64 = 0;
    return;
  }

  // If ReXGlue cleared output_buffer_valid on empty, re-enable it so the decoder can work.
  XMA_CONTEXT_DATA data(base + xma_context);
  if (!data.output_buffer_valid && data.output_buffer_ptr != 0 && data.output_buffer_block_count > 0) {
    data.output_buffer_valid = 1;
    data.Store(base + xma_context);
  }

  const ContextState before = ReadState(base, xma_context);

  PPCContext call_ctx{};
  call_ctx.r3.u64 = xma_context;
  __imp__XMAEnableContext(call_ctx, base);

  // Restore output_buffer_valid if ReXGlue cleared it:
  XMA_CONTEXT_DATA data_after(base + xma_context);
  if (!data_after.output_buffer_valid && data_after.output_buffer_ptr != 0 && data_after.output_buffer_block_count > 0) {
    data_after.output_buffer_valid = 1;
    data_after.Store(base + xma_context);
  }

  const ContextState after = ReadState(base, xma_context);
  const bool progress = (after.dword0 != before.dword0 || after.dword2 != before.dword2);

  if (progress) {
    static thread_local uint32_t retried_stream = 0;
    static thread_local uint32_t retry_count = 0;
    if (stream != retried_stream) {
      retried_stream = stream;
      retry_count = 0;
    }
    if (retry_count++ < 2) {
      return;  // Keep r26 != 0 so sub_826592F0 retries this stream
    }
    retried_stream = 0;
    retry_count = 0;
  }

  // The kick decoded synchronously. If this is the end of the sound, try flushing the final carry block:
  const uint32_t sound = g_mixer_call.sound;
  const uint32_t remaining = IsValidGuestPtr(sound) ? Load32(base, sound + 36) : 0;
  if (superman_returns::IsFinalXmaRequest(remaining, g_mixer_call.requested)) {
    if (TryFlushFinalBlock(base, xma_context, stream)) {
      // The first export returned zero in r3; restore the context argument.
      call_ctx.r3.u64 = xma_context;
      __imp__XMAEnableContext(call_ctx, base);
      const ContextState after_flush = ReadState(base, xma_context);
      if (after_flush.dword0 != before.dword0 || after_flush.dword2 != before.dword2) {
        return;
      }
    }
  }

  // Stopped waiting: the context made no progress. Never spin, never yield, never alter sound+36.
  LogGiveUp("no progress", xma_context, stream, after);
  r26.u64 = 0;
}
