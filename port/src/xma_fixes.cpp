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
//  2. The runtime clears output_buffer_valid whenever a decode pass ends with
//     the output ring's read and write offsets equal. The mixer reads a clear
//     flag as "ring full": right when the pass filled the ring, wrong when it
//     had nothing to decode into an empty one. In the second case the mixer
//     plays a whole ring of stale samples and later kicks return at once.
//     RearmEmptyOutput sets the flag again only for the empty case, which it
//     tells apart by the free-block budget the decoder left behind. Setting it
//     unconditionally makes a full ring look empty and discards its samples.
//
//  3. The runtime decoder delays each block by one frame (the tail waits in a
//     carry buffer for the next block) and has no flush when input runs out, so
//     the final frame is missing and the mixer waits for it. TryFlushFinalBlock emits
//     the carry tail followed by silence when the sound reaches its final request.
//     SR_XMA_FLUSH=0 turns the flush off.
//
//  4. When neither a kick nor the flush gives the mixer its samples, the hook
//     appends silence to the output ring instead of skipping the stream. With
//     r26 = 0 the mixer still takes the request off the sound's sample count
//     (sound+36) but returns nothing to its caller; the two counts drift, the
//     caller keeps asking a sound that has nothing queued, the voice never
//     ends, and the voice pool fills until every sound, music first, is gone.
//
// Guest memory goes through HostPtr: the contexts live at 0xFFCAxxxx, in the
// 0xE0000000 heap that Windows maps 0x1000 bytes higher than base + address.
//
// Mid-asm hook at 0x826595B8 (`cmplwi cr6,r26,0` before the retry branch);
// r31 points at the stream, whose first word points at {xma_context*, ...}.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include <rex/audio/audio_system.h>
#include <rex/audio/xma/context.h>
#include <rex/audio/xma/decoder.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

#include "generated/default/superman_returns_init.h"
#include "xma_flush_policy.h"

namespace {

using Clock = std::chrono::steady_clock;

// Safety net for a context that keeps making progress without ever giving
// the mixer enough samples. Well under one 256-sample audio frame (5.3 ms).
constexpr auto kMaxStarvedWait = std::chrono::milliseconds(1);

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
struct FreeBlocksTag {
  using Type = int32_t XmaContext::*;
  friend Type Unlock(FreeBlocksTag);
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
template struct Key<FreeBlocksTag, &XmaContext::remaining_subframe_blocks_in_output_buffer_>;
template struct Key<ContextsTag, &XmaDecoder::contexts_>;

inline bool IsValidGuestPtr(uint32_t ptr) {
  return ptr >= 0x00010000 && ptr <= 0xFFFF0000 - 4;
}

// Host bytes of a guest address. Never `base + addr`: the XMA contexts live in
// the 0xE0000000 physical heap, which Windows maps 0x1000 bytes higher, so raw
// arithmetic lands on the context 64 slots earlier in the kernel's array.
uint8_t* HostPtr(const uint8_t* base, uint32_t addr) {
  return rex::memory::GuestPtr(const_cast<uint8_t*>(base), addr);
}

uint32_t Load32(const uint8_t* base, uint32_t addr) {
  if (!base || !IsValidGuestPtr(addr)) return 0;
  return __builtin_bswap32(*reinterpret_cast<const uint32_t*>(HostPtr(base, addr)));
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
  XMA_CONTEXT_DATA data(HostPtr(base, xma_context));
  if (data.output_buffer_ptr == 0 || data.output_buffer_block_count == 0) return true;
  return false;
}

// Mixer-thread counters, reported every 10 s while they change. The per-event
// messages below are capped, so these are what shows a problem late in a session.
struct HookStats {
  uint64_t starved = 0;    // hook entries with the mixer short of samples
  uint64_t kicks = 0;      // XMAEnableContext calls made by the hook
  uint64_t progress = 0;   // kicks that changed the context
  uint64_t rearmed = 0;    // empty output rings flagged valid again
  uint64_t padded_blocks = 0;  // 256-byte blocks of silence given to the mixer
  uint64_t released = 0, timed_out = 0, no_progress = 0, nothing_queued = 0, other = 0;  // give-ups
};
HookStats g_stats;

void MaybeLogStats() {
  static Clock::time_point last = Clock::now();
  static HookStats reported;
  const auto now = Clock::now();
  if (now - last < std::chrono::seconds(10)) return;
  last = now;
  if (std::memcmp(&reported, &g_stats, sizeof(HookStats)) == 0) return;
  const HookStats& s = g_stats;
  REXAPU_INFO(
      "XMA fix: starved {} kicks {} progress {} rearmed {} silent blocks {}; gave up: released {} "
      "timed out {} no progress {} nothing queued {} other {} (cumulative)",
      s.starved, s.kicks, s.progress, s.rearmed, s.padded_blocks, s.released, s.timed_out,
      s.no_progress, s.nothing_queued, s.other);
  reported = s;
}

void LogGiveUp(const char* reason, uint32_t xma_context, uint32_t stream,
               const ContextState& state) {
  static std::atomic<uint32_t> count{0};
  if (reason[0] == 'r') ++g_stats.released;
  else if (reason[0] == 't') ++g_stats.timed_out;
  else if (reason[1] == 'o' && reason[2] == ' ') ++g_stats.no_progress;
  else if (reason[0] == 'n') ++g_stats.nothing_queued;
  else ++g_stats.other;
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
  XMA_CONTEXT_DATA data(HostPtr(base, xma_context));

  in.input_exhausted = superman_returns::IsXmaInputExhausted(
      data.input_buffer_0_valid != 0, data.input_buffer_1_valid != 0,
      data.current_buffer, data.input_buffer_0_packet_count,
      data.input_buffer_1_packet_count, data.input_buffer_read_offset,
      data.error_status);
  in.output_valid = (data.output_buffer_ptr != 0 && data.output_buffer_block_count > 0);
  in.loop_count = data.loop_count;

  std::unique_lock<std::mutex> lock;
  if (context) {
    lock = std::unique_lock<std::mutex>(context->*Unlock(LockTag{}));
    in.carry_valid = context->*Unlock(CarryValidTag{});
    in.block_pending = (context->*Unlock(RemainingSubframesTag{})) != 0;
  }

  const auto decision = superman_returns::DecideXmaFinalFlush(in);
  if (decision == superman_returns::XmaFlushDecision::LoopActive) {
    ++superman_returns::GetGlobalXmaStabilityTelemetry().loop_flush_suppressions;
  }
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
  data.Store(HostPtr(base, xma_context));

  ++superman_returns::GetGlobalXmaStabilityTelemetry().trailing_carry_flushes;
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

// The runtime clears output_buffer_valid whenever the output ring ends a pass
// with read == write. That is right for a full ring, but it also happens when
// the pass had nothing to decode into an empty ring; the mixer then reads a
// whole buffer of stale samples and later kicks do nothing. Set the flag again
// only for the empty case. Returns true when the flag was set.
bool RearmEmptyOutput(const uint8_t* base, uint32_t xma_context) {
  if (!base || !IsValidGuestPtr(xma_context)) return false;
  const XMA_CONTEXT_DATA peek(HostPtr(base, xma_context));
  if (peek.output_buffer_valid || peek.output_buffer_ptr == 0 ||
      peek.output_buffer_block_count == 0) {
    return false;
  }
  XmaContext* context = FindContext(xma_context);
  if (!context) return false;
  std::lock_guard<std::mutex> lock(context->*Unlock(LockTag{}));
  XMA_CONTEXT_DATA data(HostPtr(base, xma_context));
  if (data.output_buffer_valid ||
      !superman_returns::IsXmaOutputRingEmpty(data.output_buffer_read_offset,
                                              data.output_buffer_write_offset,
                                              data.output_buffer_block_count,
                                              context->*Unlock(FreeBlocksTag{}))) {
    return false;
  }
  data.output_buffer_valid = 1;
  data.Store(HostPtr(base, xma_context));
  ++g_stats.rearmed;
  return true;
}

// Appends one block of silence to the output ring (see XmaSilencePad). Returns
// false when the context has no room for it.
bool PadOutputWithSilence(const uint8_t* base, uint32_t xma_context) {
  XmaContext* context = FindContext(xma_context);
  if (!context) return false;
  std::lock_guard<std::mutex> lock(context->*Unlock(LockTag{}));
  XMA_CONTEXT_DATA data(HostPtr(base, xma_context));
  if (data.output_buffer_ptr == 0) return false;
  const auto pad = superman_returns::PlanXmaSilencePad(
      data.output_buffer_read_offset, data.output_buffer_write_offset,
      data.output_buffer_block_count, data.output_buffer_valid != 0);
  if (!pad.fits) return false;
  uint8_t* output =
      rex::system::kernel_state()->memory()->TranslatePhysical(data.output_buffer_ptr);
  std::memset(output + size_t(pad.write_block) * XmaContext::kOutputBytesPerBlock, 0,
              XmaContext::kOutputBytesPerBlock);
  data.output_buffer_write_offset = pad.next_write;
  if (pad.fills_ring) {
    data.output_buffer_valid = 0;
    context->*Unlock(FreeBlocksTag{}) = 0;  // keep RearmEmptyOutput reading it as full
  }
  data.Store(HostPtr(base, xma_context));
  ++g_stats.padded_blocks;
  return true;
}

// Rearms the empty output rings of every stream of a sound.
void RearmEmptyOutputs(const uint8_t* base, uint32_t sound) {
  if (!base || !IsValidGuestPtr(sound)) return;
  const uint32_t stream_count = Load32(base, sound + 32);
  const uint32_t stream_array = Load32(base, sound + 24);
  if (!IsValidGuestPtr(stream_array) || stream_count == 0 || stream_count > 64) return;
  for (uint32_t i = 0; i < stream_count; ++i) {
    const uint32_t container = Load32(base, stream_array + i * 24);
    if (!IsValidGuestPtr(container)) continue;
    RearmEmptyOutput(base, Load32(base, container));
  }
}

}  // namespace

// sub_826586A0 supplies input packets to streams of a sound.
// Rearm empty output rings so the newly supplied packets are decoded.
REX_HOOK_RAW(sub_826586A0) {
  const uint32_t sound = ctx.r3.u32;
  RearmEmptyOutputs(base, sound);
  __imp__sub_826586A0(ctx, base);
  RearmEmptyOutputs(base, sound);
}

// sub_826592F0 is the audio mixer; remember the sound and the request for the
// mid-asm hook below.
REX_HOOK_RAW(sub_826592F0) {
  const uint32_t sound = ctx.r3.u32;
  RearmEmptyOutputs(base, sound);
  const MixerCall saved = g_mixer_call;
  g_mixer_call = {sound, static_cast<int32_t>(ctx.r5.u32)};
  __imp__sub_826592F0(ctx, base);
  g_mixer_call = saved;
  RearmEmptyOutputs(base, sound);
}

void XmaKickStarvedContext(PPCRegister& r26, PPCRegister& r31) {
  // The audio mixer runs on a single guest thread.
  static thread_local uint32_t waiting_stream = 0;
  static thread_local Clock::time_point waiting_since;

  MaybeLogStats();
  if (r26.u32 == 0) {  // Enough samples; the mixer is not waiting.
    waiting_stream = 0;
    return;
  }
  ++g_stats.starved;

  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  const uint32_t stream = r31.u32;
  uint32_t xma_context = 0;

  auto give_up = [&](const char* reason, const ContextState& state = {}) {
    LogGiveUp(reason, xma_context, stream, state);
    waiting_stream = 0;
    r26.u64 = 0;
  };

  if (!IsValidGuestPtr(stream)) {
    give_up("invalid stream");
    return;
  }
  const uint32_t container = Load32(base, stream);
  if (!IsValidGuestPtr(container)) {
    give_up("invalid container");
    return;
  }
  xma_context = Load32(base, container);
  if (!IsValidGuestPtr(xma_context)) {
    give_up("released");
    return;
  }

  const ContextState before_pre = ReadState(base, xma_context);
  XmaContext* context = FindContext(xma_context);
  if (IsDeadContext(base, xma_context, before_pre, context)) {
    give_up("released", before_pre);
    return;
  }

  const auto now = Clock::now();
  if (stream != waiting_stream) {
    waiting_stream = stream;
    waiting_since = now;
  } else if (now - waiting_since > kMaxStarvedWait) {
    give_up("timed out", before_pre);
    return;
  }

  // The mixer asks for zero samples once the sound has nothing queued; no
  // output can satisfy that, and skipping the stream costs nothing.
  const uint32_t sound = g_mixer_call.sound;
  const uint32_t remaining = IsValidGuestPtr(sound) ? Load32(base, sound + 36) : 0;
  if (remaining == 0) {
    give_up("nothing queued", before_pre);
    return;
  }

  // An empty ring flagged like a full one would make the kick a no-op.
  RearmEmptyOutput(base, xma_context);
  const ContextState before = ReadState(base, xma_context);

  PPCContext call_ctx{};
  call_ctx.r3.u64 = xma_context;
  ++g_stats.kicks;
  __imp__XMAEnableContext(call_ctx, base);

  // Same repair after the kick, so a pass that decoded nothing compares equal.
  RearmEmptyOutput(base, xma_context);
  const ContextState after = ReadState(base, xma_context);
  if (after != before) {
    ++g_stats.progress;
    return;  // Let the mixer look at the stream again.
  }

  // The kick decoded synchronously and changed nothing, and the packets it
  // needs come from the feeder that runs after this loop: waiting cannot help.
  // At the end of the sound the decoder still holds its last block.
  if (superman_returns::IsFinalXmaRequest(remaining, g_mixer_call.requested) &&
      TryFlushFinalBlock(base, xma_context, stream)) {
    // The first export returned zero in r3; restore the context argument.
    call_ctx.r3.u64 = xma_context;
    __imp__XMAEnableContext(call_ctx, base);
    if (ReadState(base, xma_context) != before) return;
  }

  // Otherwise hand the mixer silence, a block per retry, until it has what it
  // asked for. Leaving the stream out instead (r26 = 0) would still take the
  // samples off the sound without returning them to the caller.
  if (PadOutputWithSilence(base, xma_context)) return;

  give_up("no progress", after);
}
