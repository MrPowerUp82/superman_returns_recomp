#include "../../port/src/xma_flush_policy.h"
#include "test_main.h"

using namespace superman_returns;

namespace {

XmaFlushInputs EndOfStream() {
  XmaFlushInputs in;
  in.context_found = true;
  in.input_exhausted = true;
  in.output_valid = true;
  in.carry_valid = true;
  in.final_request = true;
  in.last_segment = true;
  return in;
}

}  // namespace

SR_TEST(xma_flush_happens_only_when_every_sign_says_the_stream_ended) {
  SR_CHECK(DecideXmaFinalFlush(EndOfStream()) == XmaFlushDecision::Flush);
}

SR_TEST(xma_flush_waits_for_input_of_a_streamed_voice) {
  XmaFlushInputs in = EndOfStream();
  in.input_exhausted = false;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::InputPending);
}

SR_TEST(xma_flush_skips_a_released_or_unreadable_context) {
  XmaFlushInputs in = EndOfStream();
  in.context_found = false;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::NoContext);

  in = EndOfStream();
  in.output_valid = false;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::OutputInvalid);
}

SR_TEST(xma_flush_needs_a_carried_block_and_never_repeats) {
  XmaFlushInputs in = EndOfStream();
  in.carry_valid = false;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::NoCarry);

  in = EndOfStream();
  in.block_pending = true;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::BlockPending);
}

SR_TEST(xma_flush_keeps_a_sound_with_samples_left_or_segments_queued) {
  XmaFlushInputs in = EndOfStream();
  in.final_request = false;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::NotFinalRequest);

  in = EndOfStream();
  in.last_segment = false;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::MoreSegments);
}

SR_TEST(xma_input_exhausted_when_neither_buffer_valid) {
  SR_CHECK(IsXmaInputExhausted(false, false, 0, 1, 1, 0, 0));
}

SR_TEST(xma_input_exhausted_on_error_status_4_with_single_buffer) {
  SR_CHECK(IsXmaInputExhausted(true, false, 0, 1, 0, 100, 4));
  SR_CHECK(IsXmaInputExhausted(false, true, 1, 0, 1, 100, 4));
  // If other buffer is valid, not exhausted:
  SR_CHECK(!IsXmaInputExhausted(true, true, 0, 1, 1, 100, 4));
}

SR_TEST(xma_input_exhausted_when_read_offset_reaches_capacity) {
  SR_CHECK(IsXmaInputExhausted(true, false, 0, 2, 0, 4096, 0));
  SR_CHECK(IsXmaInputExhausted(true, false, 0, 2, 0, 5000, 0));
  SR_CHECK(!IsXmaInputExhausted(true, false, 0, 2, 0, 2048, 0));
  SR_CHECK(!IsXmaInputExhausted(true, true, 0, 2, 2, 4096, 0));
}

SR_TEST(xma_final_request_takes_everything_the_segment_has_left) {
  SR_CHECK(IsFinalXmaRequest(256, 256));
  SR_CHECK(IsFinalXmaRequest(100, 512));
  SR_CHECK(!IsFinalXmaRequest(1000, 512));
  SR_CHECK(!IsFinalXmaRequest(0, 512));
  SR_CHECK(!IsFinalXmaRequest(100, 0));
  SR_CHECK(!IsFinalXmaRequest(100, -5));
}

SR_TEST(xma_last_segment_means_no_next_node) {
  SR_CHECK(IsLastXmaSegment(0, 0));
  SR_CHECK(IsLastXmaSegment(0x40001000, 0));
  SR_CHECK(!IsLastXmaSegment(0x40001000, 0x40002000));
}
