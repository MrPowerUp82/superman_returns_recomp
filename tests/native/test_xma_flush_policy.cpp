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
  // Two packets of 2048 bytes occupy 32768 bits, not 4096 bits.
  for (uint32_t current : {0u, 1u}) {
    for (uint32_t offset : {4096u, 5000u, 32767u}) {
      SR_CHECK(!IsXmaInputExhausted(current == 0, current == 1, current,
                                   2, 2, offset, 0));
    }
    SR_CHECK(IsXmaInputExhausted(current == 0, current == 1, current,
                               2, 2, 32768, 0));
    SR_CHECK(IsXmaInputExhausted(current == 0, current == 1, current,
                               2, 2, 32769, 0));
    SR_CHECK(!IsXmaInputExhausted(true, true, current, 2, 2, 32768, 0));
  }
}

SR_TEST(xma_input_capacity_uses_the_active_buffer_packet_count) {
  SR_CHECK(!IsXmaInputExhausted(true, false, 0, 1, 2, 16383, 0));
  SR_CHECK(IsXmaInputExhausted(true, false, 0, 1, 2, 16384, 0));
  SR_CHECK(!IsXmaInputExhausted(false, true, 1, 1, 2, 16384, 0));
  SR_CHECK(IsXmaInputExhausted(false, true, 1, 1, 2, 32768, 0));
}

SR_TEST(xma_final_flush_waits_until_all_packet_bits_are_consumed) {
  XmaFlushInputs in = EndOfStream();
  in.input_exhausted = IsXmaInputExhausted(true, false, 0, 2, 0, 4096, 0);
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::InputPending);
  in.input_exhausted = IsXmaInputExhausted(true, false, 0, 2, 0, 32768, 0);
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::Flush);
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

SR_TEST(xma_flush_policy_suppresses_flush_when_loop_active) {
  ResetGlobalXmaStabilityTelemetry();
  XmaFlushInputs in = EndOfStream();
  in.loop_count = 0;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::Flush);

  in.loop_count = 1;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::LoopActive);

  in.loop_count = 255;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::LoopActive);
}

SR_TEST(xma_stream_lifecycle_tracker_multi_transition_soak) {
  XmaStreamLifecycleTracker tracker;
  tracker.Reset();

  const uint32_t kCycles = 1200;
  for (uint32_t i = 0; i < kCycles; ++i) {
    const uint32_t initial_loops = (i % 5 == 0) ? 0 : ((i % 5) + 1);
    tracker.StartStream(initial_loops);

    if (initial_loops > 0) {
      SR_CHECK(tracker.state == XmaStreamLifecycleState::Looping);
    } else {
      SR_CHECK(tracker.state == XmaStreamLifecycleState::Active);
    }

    XmaFlushInputs in = EndOfStream();
    in.input_exhausted = false;
    in.loop_count = tracker.current_loop_count;
    SR_CHECK(tracker.EvaluateFlushDecision(in) == (in.loop_count > 0 ? XmaFlushDecision::LoopActive : XmaFlushDecision::InputPending));

    in.input_exhausted = true;
    tracker.NotifyStarvation();

    while (tracker.current_loop_count > 0) {
      in.loop_count = tracker.current_loop_count;
      SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::LoopActive);
      SR_CHECK(tracker.carry_pending);
      tracker.RecordStarvationRescue();
      tracker.StepLoopBoundary();
    }

    if (i % 50 == 0) {
      tracker.RecordBitOffsetClamp();
      tracker.RecordInvalidBufferWriteGuard();
    }

    in.loop_count = 0;
    SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::Flush);
    SR_CHECK(!tracker.carry_pending);
    tracker.NotifyExhaustion();
    SR_CHECK(tracker.state == XmaStreamLifecycleState::Exhausted);
  }

  SR_CHECK(tracker.telemetry.context_transitions >= kCycles);
  SR_CHECK(tracker.telemetry.loop_flush_suppressions > 0);
  SR_CHECK_EQ(tracker.telemetry.trailing_carry_flushes, kCycles);
  SR_CHECK(tracker.telemetry.starvation_rescues > 0);
  SR_CHECK(tracker.telemetry.bit_offset_clamps > 0);
  SR_CHECK(tracker.telemetry.invalid_buffer_write_guards > 0);
}
