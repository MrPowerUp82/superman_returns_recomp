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
  bool input_exhausted = false;   // neither input buffer is valid
  bool output_valid = false;      // the guest still reads the output buffer
  bool carry_valid = false;       // a decoded block is waiting for its successor
  bool block_pending = false;     // subframes of the current block still wait for output space
  bool final_request = false;     // the mixer asks for every sample the sound has left
  bool last_segment = false;      // the sound has no further queued segment
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
};

inline XmaFlushDecision DecideXmaFinalFlush(const XmaFlushInputs& in) {
  if (!in.context_found) return XmaFlushDecision::NoContext;
  if (!in.input_exhausted) return XmaFlushDecision::InputPending;
  if (!in.output_valid) return XmaFlushDecision::OutputInvalid;
  if (!in.carry_valid) return XmaFlushDecision::NoCarry;
  if (in.block_pending) return XmaFlushDecision::BlockPending;
  if (!in.final_request) return XmaFlushDecision::NotFinalRequest;
  if (!in.last_segment) return XmaFlushDecision::MoreSegments;
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
  }
  return "?";
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

}  // namespace superman_returns
