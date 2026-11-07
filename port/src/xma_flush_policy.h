// When may the XMA fix emit the decoder's withheld final block?
//
// The runtime's XMA decoder delays every block by one frame: the tail of a
// decoded block waits in a carry buffer until the next block supplies the
// head of the output frame. A stream's last block therefore never leaves the
// decoder, and the audio mixer (sub_826592F0) keeps asking for samples that
// cannot arrive. Emitting the carry early would corrupt a streamed voice that
// is only waiting for its next disc read, so the final block is flushed only
// when every sign says the stream is over.
#pragma once

#include <cstdint>

namespace superman_returns {

struct XmaFlushInputs {
  bool context_found = false;     // the runtime has an allocated context for the guest pointer
  bool input_exhausted = false;   // neither input buffer is valid, or active buffer reached end / error
  bool output_valid = false;      // the guest still reads the output buffer (output buffer allocated)
  bool carry_valid = false;       // a decoded block is waiting for its successor
  bool block_pending = false;     // subframes of the current block still wait for output space
  bool final_request = false;     // the mixer asks for every sample the sound has left
  bool last_segment = false;      // the sound has no further queued segment
  uint32_t loop_count = 0;        // active loop count (AUD-07: never flush carry on looping streams)
};

enum class XmaFlushDecision : uint8_t {
  Flush,
  NoContext,
  InputPending,
  OutputInvalid,
  NoCarry,
  BlockPending,
  NotFinalRequest,
  MoreSegments,
  LoopActive,
};

inline XmaFlushDecision DecideXmaFinalFlush(const XmaFlushInputs& in) {
  if (!in.context_found) return XmaFlushDecision::NoContext;
  if (!in.final_request) return XmaFlushDecision::NotFinalRequest;
  if (!in.last_segment) return XmaFlushDecision::MoreSegments;
  if (in.loop_count > 0) return XmaFlushDecision::LoopActive;
  if (!in.output_valid) return XmaFlushDecision::OutputInvalid;
  if (!in.carry_valid) return XmaFlushDecision::NoCarry;
  if (in.block_pending) return XmaFlushDecision::BlockPending;
  if (!in.input_exhausted) return XmaFlushDecision::InputPending;
  return XmaFlushDecision::Flush;
}

inline const char* XmaFlushDecisionName(XmaFlushDecision decision) {
  switch (decision) {
    case XmaFlushDecision::Flush: return "flush";
    case XmaFlushDecision::NoContext: return "no context";
    case XmaFlushDecision::InputPending: return "input pending";
    case XmaFlushDecision::OutputInvalid: return "output invalid";
    case XmaFlushDecision::NoCarry: return "no carried block";
    case XmaFlushDecision::BlockPending: return "block pending";
    case XmaFlushDecision::NotFinalRequest: return "not the final request";
    case XmaFlushDecision::MoreSegments: return "more segments queued";
    case XmaFlushDecision::LoopActive: return "loop active";
  }
  return "?";
}

// Determines if input is exhausted. ReXGlue only swaps buffers at packet boundaries;
// if the decoder hits end-of-packets (error_status == 4) or read_offset traverses all
// packet bits of the active buffer without a valid alternate buffer, the input is exhausted.
inline constexpr uint32_t kXmaBitsPerPacket = 2048u * 8u;  // 2048 bytes per packet.

inline bool IsXmaInputExhausted(bool in0_valid, bool in1_valid, uint32_t current_buffer,
                               uint32_t in0_packet_count, uint32_t in1_packet_count,
                               uint32_t read_offset_bits, uint32_t error_status) {
  if (!in0_valid && !in1_valid) return true;
  const bool other_valid = (current_buffer == 0) ? in1_valid : in0_valid;
  if (error_status == 4 && !other_valid) return true;
  const uint32_t cur_packets = (current_buffer == 0) ? in0_packet_count : in1_packet_count;
  const uint32_t cur_capacity_bits = cur_packets * kXmaBitsPerPacket;
  if (cur_capacity_bits > 0 && read_offset_bits >= cur_capacity_bits && !other_valid) return true;
  return false;
}

// The mixer's per-pass request is min(asked, remaining): it is the sound's last
// request when it takes everything the current segment has left.
inline bool IsFinalXmaRequest(uint32_t segment_remaining, int32_t samples_requested) {
  return segment_remaining != 0 && samples_requested > 0 &&
         static_cast<uint32_t>(samples_requested) >= segment_remaining;
}

// The sound keeps its queued segments in a list whose head is the segment in
// progress; each node starts with the next node's address.
inline bool IsLastXmaSegment(uint32_t head, uint32_t next_of_head) {
  return head == 0 || next_of_head == 0;
}

struct XmaStabilityTelemetry {
  uint64_t starvation_rescues = 0;
  uint64_t trailing_carry_flushes = 0;
  uint64_t loop_flush_suppressions = 0;
  uint64_t invalid_buffer_write_guards = 0;
  uint64_t bit_offset_clamps = 0;
  uint64_t context_transitions = 0;

  void Reset() { *this = XmaStabilityTelemetry{}; }
};

inline XmaStabilityTelemetry& GetGlobalXmaStabilityTelemetry() {
  static XmaStabilityTelemetry s_telemetry;
  return s_telemetry;
}

inline void ResetGlobalXmaStabilityTelemetry() {
  GetGlobalXmaStabilityTelemetry().Reset();
}

enum class XmaStreamLifecycleState : uint8_t {
  Idle = 0,
  Active,
  Looping,
  Starved,
  Exhausted,
};

class XmaStreamLifecycleTracker {
 public:
  XmaStabilityTelemetry telemetry{};
  XmaStreamLifecycleState state = XmaStreamLifecycleState::Idle;
  uint32_t current_loop_count = 0;
  bool carry_pending = false;

  void StartStream(uint32_t loop_count = 0) {
    current_loop_count = loop_count;
    state = (loop_count > 0) ? XmaStreamLifecycleState::Looping : XmaStreamLifecycleState::Active;
    carry_pending = true;
    ++telemetry.context_transitions;
  }

  void RecordStarvationRescue() {
    ++telemetry.starvation_rescues;
    if (state == XmaStreamLifecycleState::Starved) {
      state = (current_loop_count > 0) ? XmaStreamLifecycleState::Looping
                                       : XmaStreamLifecycleState::Active;
      ++telemetry.context_transitions;
    }
  }

  void RecordInvalidBufferWriteGuard() { ++telemetry.invalid_buffer_write_guards; }
  void RecordBitOffsetClamp() { ++telemetry.bit_offset_clamps; }

  void StepLoopBoundary() {
    if (current_loop_count > 0) {
      if (current_loop_count != 0xFFu && current_loop_count != 0xFFFFFFFFu) {
        --current_loop_count;
      }
      if (current_loop_count == 0) {
        state = XmaStreamLifecycleState::Active;
        ++telemetry.context_transitions;
      }
    }
  }

  void NotifyStarvation() {
    if (state != XmaStreamLifecycleState::Starved) {
      state = XmaStreamLifecycleState::Starved;
      ++telemetry.context_transitions;
    }
  }

  void NotifyExhaustion() {
    if (state != XmaStreamLifecycleState::Exhausted) {
      state = XmaStreamLifecycleState::Exhausted;
      carry_pending = false;
      ++telemetry.context_transitions;
    }
  }

  XmaFlushDecision EvaluateFlushDecision(const XmaFlushInputs& in) {
    const XmaFlushDecision decision = DecideXmaFinalFlush(in);
    if (decision == XmaFlushDecision::LoopActive) {
      ++telemetry.loop_flush_suppressions;
    } else if (decision == XmaFlushDecision::Flush) {
      ++telemetry.trailing_carry_flushes;
      carry_pending = false;
    }
    return decision;
  }

  void Reset() {
    state = XmaStreamLifecycleState::Idle;
    current_loop_count = 0;
    carry_pending = false;
    telemetry.Reset();
  }
};

}  // namespace superman_returns
