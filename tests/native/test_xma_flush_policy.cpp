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

// ============================================================================
// AUD-10 Regression Scenarios
// ============================================================================

// Scenario 1: Looping music must survive buffer transitions and loop boundaries.
// Premature carry flushes cut background music right after takeoff or between cues.
SR_TEST(xma_scenario_post_takeoff_music_loop_continuity) {
  XmaStreamLifecycleTracker tracker;
  tracker.Reset();

  // 1A: Infinite loop (loop_count = 255, standard for background / flight music)
  tracker.StartStream(255);
  SR_CHECK(tracker.state == XmaStreamLifecycleState::Looping);
  SR_CHECK_EQ(tracker.current_loop_count, 255u);

  // Cycle through multiple packet buffer swaps across loop boundaries.
  // In Xbox 360 XMA streaming, buffers in0 and in1 alternate as packets are fetched.
  for (uint32_t cycle = 0; cycle < 16; ++cycle) {
    const bool current_is_zero = (cycle % 2 == 0);
    const uint32_t cur_buf = current_is_zero ? 0u : 1u;

    // Buffer is currently reading and alternate buffer is valid or loading:
    bool in0_valid = true;
    bool in1_valid = true;
    bool input_exhausted = IsXmaInputExhausted(in0_valid, in1_valid, cur_buf, 2, 2, 16384, 0);
    SR_CHECK(!input_exhausted);

    XmaFlushInputs in = EndOfStream();
    in.input_exhausted = input_exhausted;
    in.loop_count = tracker.current_loop_count;
    SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::LoopActive);
    SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::LoopActive);
    SR_CHECK(tracker.carry_pending);

    // Now active buffer reaches packet capacity before next buffer arrives:
    input_exhausted = IsXmaInputExhausted(current_is_zero, !current_is_zero, cur_buf, 2, 2, 32768, 0);
    SR_CHECK(input_exhausted);
    in.input_exhausted = input_exhausted;

    // Even if input appears exhausted at packet boundary, loop count must suppress carry flush!
    SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::LoopActive);
    SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::LoopActive);
    SR_CHECK(tracker.carry_pending);

    // Cross loop boundary: infinite loop counter (0xFF) must NOT decrement:
    tracker.StepLoopBoundary();
    SR_CHECK_EQ(tracker.current_loop_count, 255u);
    SR_CHECK(tracker.state == XmaStreamLifecycleState::Looping);
  }
  SR_CHECK_EQ(tracker.telemetry.trailing_carry_flushes, 0u);
  SR_CHECK(tracker.telemetry.loop_flush_suppressions > 0);

  // 1B: Finite loop (e.g., loop_count = 4 repeats)
  tracker.Reset();
  tracker.StartStream(4);
  SR_CHECK(tracker.state == XmaStreamLifecycleState::Looping);

  for (uint32_t expected_remaining = 4; expected_remaining > 0; --expected_remaining) {
    SR_CHECK_EQ(tracker.current_loop_count, expected_remaining);
    XmaFlushInputs in = EndOfStream();
    in.loop_count = tracker.current_loop_count;
    SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::LoopActive);
    SR_CHECK(tracker.carry_pending);
    tracker.StepLoopBoundary();
  }

  // After 4 loop boundaries, loop count reaches 0 and stream transitions to Active:
  SR_CHECK_EQ(tracker.current_loop_count, 0u);
  SR_CHECK(tracker.state == XmaStreamLifecycleState::Active);

  // Once loop completes and stream ends, flush is allowed exactly once:
  XmaFlushInputs final_in = EndOfStream();
  final_in.loop_count = 0;
  SR_CHECK(tracker.EvaluateFlushDecision(final_in) == XmaFlushDecision::Flush);
  SR_CHECK(!tracker.carry_pending);
  SR_CHECK_EQ(tracker.telemetry.trailing_carry_flushes, 1u);
}

// Scenario 2: Jor-El dialogue lines are multi-segment chained audio.
// Intermediate segments exhaust their packet buffers and request final samples,
// but carry must be withheld until the very last segment in the chain.
SR_TEST(xma_scenario_jor_el_multi_segment_dialogue_chain) {
  XmaStreamLifecycleTracker tracker;
  tracker.Reset();
  tracker.StartStream(0);

  // Segment 1 of 3: head at 0x40001000, next segment at 0x40002000
  uint32_t head = 0x40001000;
  uint32_t next = 0x40002000;
  SR_CHECK(!IsLastXmaSegment(head, next));
  SR_CHECK(IsFinalXmaRequest(256, 256));

  XmaFlushInputs in1 = EndOfStream();
  in1.last_segment = IsLastXmaSegment(head, next);
  in1.final_request = IsFinalXmaRequest(256, 256);
  in1.input_exhausted = true;
  SR_CHECK(DecideXmaFinalFlush(in1) == XmaFlushDecision::MoreSegments);
  SR_CHECK(tracker.EvaluateFlushDecision(in1) == XmaFlushDecision::MoreSegments);
  SR_CHECK(tracker.carry_pending);

  // Segment 2 of 3: head advances to 0x40002000, next segment at 0x40003000
  head = 0x40002000;
  next = 0x40003000;
  SR_CHECK(!IsLastXmaSegment(head, next));

  XmaFlushInputs in2 = EndOfStream();
  in2.last_segment = IsLastXmaSegment(head, next);
  in2.final_request = IsFinalXmaRequest(256, 256);
  in2.input_exhausted = true;
  SR_CHECK(DecideXmaFinalFlush(in2) == XmaFlushDecision::MoreSegments);
  SR_CHECK(tracker.EvaluateFlushDecision(in2) == XmaFlushDecision::MoreSegments);
  SR_CHECK(tracker.carry_pending);

  // Segment 3 of 3: final segment (head at 0x40003000, next is 0)
  head = 0x40003000;
  next = 0;
  SR_CHECK(IsLastXmaSegment(head, next));

  XmaFlushInputs in3 = EndOfStream();
  in3.last_segment = IsLastXmaSegment(head, next);
  in3.final_request = IsFinalXmaRequest(256, 256);
  in3.input_exhausted = true;
  SR_CHECK(DecideXmaFinalFlush(in3) == XmaFlushDecision::Flush);
  SR_CHECK(tracker.EvaluateFlushDecision(in3) == XmaFlushDecision::Flush);
  SR_CHECK(!tracker.carry_pending);
  SR_CHECK_EQ(tracker.telemetry.trailing_carry_flushes, 1u);

  // Following calls after final segment flush must return NoCarry (never duplicate tail block)
  in3.carry_valid = false;
  SR_CHECK(DecideXmaFinalFlush(in3) == XmaFlushDecision::NoCarry);
  SR_CHECK(tracker.EvaluateFlushDecision(in3) == XmaFlushDecision::NoCarry);
  SR_CHECK_EQ(tracker.telemetry.trailing_carry_flushes, 1u);
}

// Scenario 3: Cinematic audio streams continuously via double-buffering.
// When one buffer completes (read_offset >= capacity or error_status 4), as long as
// the alternate buffer is valid, the stream is NOT exhausted and flush must wait.
SR_TEST(xma_scenario_cinematics_double_buffer_streaming) {
  XmaStreamLifecycleTracker tracker;
  tracker.Reset();
  tracker.StartStream(0);

  // Step 1: Both buffers valid. Buffer 0 active with 2 packets (32768 bits).
  // Read offset reaches exact capacity:
  bool exhausted = IsXmaInputExhausted(true, true, 0, 2, 2, 32768, 0);
  SR_CHECK(!exhausted);

  XmaFlushInputs in = EndOfStream();
  in.input_exhausted = exhausted;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::InputPending);
  SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::InputPending);
  SR_CHECK(tracker.carry_pending);

  // Step 2: Buffer 0 reports error_status = 4 (end-of-packets), but buffer 1 is still valid:
  exhausted = IsXmaInputExhausted(true, true, 0, 2, 2, 100, 4);
  SR_CHECK(!exhausted);
  in.input_exhausted = exhausted;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::InputPending);
  SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::InputPending);

  // Step 3: Swap to Buffer 1. Buffer 0 is now refilling (valid).
  // Buffer 1 reaches capacity, but new buffer 0 is ready:
  exhausted = IsXmaInputExhausted(true, true, 1, 2, 2, 32768, 0);
  SR_CHECK(!exhausted);
  in.input_exhausted = exhausted;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::InputPending);

  // Step 4: Final cinematic scene reached. Buffer 0 invalid, Buffer 1 active and exhausted:
  exhausted = IsXmaInputExhausted(false, true, 1, 0, 2, 32768, 0);
  SR_CHECK(exhausted);
  in.input_exhausted = exhausted;
  in.last_segment = true;
  in.final_request = true;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::Flush);
  SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::Flush);
  SR_CHECK(!tracker.carry_pending);
  SR_CHECK_EQ(tracker.telemetry.trailing_carry_flushes, 1u);

  // Step 5: Both buffers invalid:
  SR_CHECK(IsXmaInputExhausted(false, false, 0, 0, 0, 0, 0));
}

// Scenario 4: Abrupt level transitions or scene quits release XMA contexts
// and invalidate output buffers. Decoder must guard against dangling guest writes
// and avoid spurious flushes.
SR_TEST(xma_scenario_level_transition_released_context_and_invalid_output) {
  XmaStreamLifecycleTracker tracker;
  tracker.Reset();
  tracker.StartStream(0);

  // Case A: Context released mid-stream:
  XmaFlushInputs in = EndOfStream();
  in.context_found = false;
  in.input_exhausted = false;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::NoContext);
  SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::NoContext);
  SR_CHECK(tracker.carry_pending);

  // Case B: Context released at EOS:
  in.input_exhausted = true;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::NoContext);
  SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::NoContext);
  SR_CHECK(tracker.carry_pending);

  // Case C: Context exists, but output buffer is invalid mid-stream:
  in.context_found = true;
  in.output_valid = false;
  in.input_exhausted = false;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::OutputInvalid);
  SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::OutputInvalid);
  tracker.RecordInvalidBufferWriteGuard();
  SR_CHECK_EQ(tracker.telemetry.invalid_buffer_write_guards, 1u);

  // Case D: Output buffer invalid at EOS:
  in.input_exhausted = true;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::OutputInvalid);
  SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::OutputInvalid);
  tracker.RecordInvalidBufferWriteGuard();
  SR_CHECK_EQ(tracker.telemetry.invalid_buffer_write_guards, 2u);

  // No trailing flushes occurred during the level transition teardown:
  SR_CHECK_EQ(tracker.telemetry.trailing_carry_flushes, 0u);
}

// Scenario 5: Disc/streaming starvation when assets stream during flight.
// Tracker enters Starved state without hanging, and upon new packet delivery,
// recovers cleanly to Active or Looping via starvation rescue and offset clamp.
SR_TEST(xma_scenario_continuous_playback_starvation_rescue_without_infinite_wait) {
  XmaStreamLifecycleTracker tracker;
  tracker.Reset();

  // Part 1: Non-looping linear audio stream starvation and rescue
  tracker.StartStream(0);
  SR_CHECK(tracker.state == XmaStreamLifecycleState::Active);

  // Background asset streaming causes audio buffer underrun:
  tracker.NotifyStarvation();
  SR_CHECK(tracker.state == XmaStreamLifecycleState::Starved);

  // Decoder should hold carry and not treat starvation as stream termination:
  XmaFlushInputs in = EndOfStream();
  in.input_exhausted = false;
  SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::InputPending);
  SR_CHECK(tracker.carry_pending);

  // Disc reads succeed; clamp corrupt/misaligned bit offsets and rescue stream:
  tracker.RecordBitOffsetClamp();
  tracker.RecordStarvationRescue();
  SR_CHECK(tracker.state == XmaStreamLifecycleState::Active);
  SR_CHECK_EQ(tracker.telemetry.starvation_rescues, 1u);
  SR_CHECK_EQ(tracker.telemetry.bit_offset_clamps, 1u);

  // Part 2: Looping audio stream starvation and rescue
  tracker.StartStream(255);
  SR_CHECK(tracker.state == XmaStreamLifecycleState::Looping);
  SR_CHECK_EQ(tracker.current_loop_count, 255u);

  tracker.NotifyStarvation();
  SR_CHECK(tracker.state == XmaStreamLifecycleState::Starved);

  // Loop suppression must still hold while starved:
  in.loop_count = tracker.current_loop_count;
  in.input_exhausted = true;
  SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::LoopActive);

  // Rescue recovers directly to Looping state:
  tracker.RecordStarvationRescue();
  SR_CHECK(tracker.state == XmaStreamLifecycleState::Looping);
  SR_CHECK_EQ(tracker.current_loop_count, 255u);
  SR_CHECK_EQ(tracker.telemetry.starvation_rescues, 2u);
}

// Scenario 6: Pausing playback (e.g. pause menu or cutscene pause)
// halts mixer requests (samples_requested == 0 or partial request).
// Withheld carry and subframe state must survive pause and resume unchanged.
SR_TEST(xma_scenario_pause_and_resume_preserves_carry_and_segment_state) {
  XmaStreamLifecycleTracker tracker;
  tracker.Reset();
  tracker.StartStream(0);

  // Normal playback before pause:
  SR_CHECK(!IsFinalXmaRequest(512, 128));

  // Game paused: mixer asks for 0 samples:
  SR_CHECK(!IsFinalXmaRequest(512, 0));

  XmaFlushInputs in = EndOfStream();
  in.final_request = IsFinalXmaRequest(512, 0);  // false
  in.carry_valid = true;
  in.block_pending = true;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::NotFinalRequest);
  SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::NotFinalRequest);
  SR_CHECK(tracker.carry_pending);

  // Subframes still pending in output queue:
  in.final_request = true;
  in.block_pending = true;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::BlockPending);
  SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::BlockPending);
  SR_CHECK(tracker.carry_pending);

  // Game unpaused and playback resumes:
  // Mixer requests remaining samples:
  in.block_pending = false;
  in.final_request = IsFinalXmaRequest(128, 128);  // true
  in.input_exhausted = true;
  in.last_segment = true;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::Flush);
  SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::Flush);
  SR_CHECK(!tracker.carry_pending);
  SR_CHECK_EQ(tracker.telemetry.trailing_carry_flushes, 1u);
}

// Scenario 7: At end-of-stream, carry is emitted exactly once. Subsequent
// calls must return NoCarry (preventing tail repeat). When a sound is re-triggered
// or repeated, StartStream() must cleanly re-initialize the lifecycle tracker.
SR_TEST(xma_scenario_stream_end_and_repeat_reinitialization) {
  XmaStreamLifecycleTracker tracker;
  tracker.Reset();
  tracker.StartStream(0);
  SR_CHECK(tracker.carry_pending);
  SR_CHECK(tracker.state == XmaStreamLifecycleState::Active);

  // First pass reaches final flush:
  XmaFlushInputs in = EndOfStream();
  SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::Flush);
  SR_CHECK(!tracker.carry_pending);
  SR_CHECK_EQ(tracker.telemetry.trailing_carry_flushes, 1u);

  tracker.NotifyExhaustion();
  SR_CHECK(tracker.state == XmaStreamLifecycleState::Exhausted);

  // Subsequent decoder pass: carry buffer is now empty (carry_valid = false):
  in.carry_valid = false;
  SR_CHECK(DecideXmaFinalFlush(in) == XmaFlushDecision::NoCarry);
  SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::NoCarry);
  // Verify trailing flush count did NOT increment again (no duplicate audio pop/tail repeat):
  SR_CHECK_EQ(tracker.telemetry.trailing_carry_flushes, 1u);

  // Sound is re-triggered / repeated (e.g. repeated sound effect or re-initialized stream):
  tracker.StartStream(0);
  SR_CHECK(tracker.carry_pending);
  SR_CHECK(tracker.state == XmaStreamLifecycleState::Active);

  // New stream with valid carry can flush when it finishes:
  in.carry_valid = true;
  SR_CHECK(tracker.EvaluateFlushDecision(in) == XmaFlushDecision::Flush);
  SR_CHECK(!tracker.carry_pending);
  SR_CHECK_EQ(tracker.telemetry.trailing_carry_flushes, 2u);
}

// AUD-11: the decoder clears output_buffer_valid for an empty ring as well as a
// full one; only its free-block budget tells them apart.
SR_TEST(xma_output_ring_is_empty_only_when_the_decoder_left_every_block_free) {
  SR_CHECK(IsXmaOutputRingEmpty(5, 5, 24, 24));    // nothing decoded into an empty ring
  SR_CHECK(!IsXmaOutputRingEmpty(5, 5, 24, 0));    // the pass filled the ring
  SR_CHECK(!IsXmaOutputRingEmpty(5, 5, 24, -2));   // filled, padding charged
  SR_CHECK(!IsXmaOutputRingEmpty(5, 9, 24, 24));   // unread blocks remain
  SR_CHECK(!IsXmaOutputRingEmpty(0, 0, 0, 0));     // no output buffer
}

SR_TEST(xma_silence_pad_appends_one_block_and_reports_a_full_ring) {
  auto pad = PlanXmaSilencePad(4, 4, 24, true);  // empty ring
  SR_CHECK(pad.fits);
  SR_CHECK_EQ(pad.write_block, 4u);
  SR_CHECK_EQ(pad.next_write, 5u);
  SR_CHECK(!pad.fills_ring);

  pad = PlanXmaSilencePad(0, 23, 24, true);  // last free block, wraps onto the reader
  SR_CHECK(pad.fits);
  SR_CHECK_EQ(pad.next_write, 0u);
  SR_CHECK(pad.fills_ring);

  SR_CHECK(!PlanXmaSilencePad(4, 4, 24, false).fits);  // full ring
  SR_CHECK(!PlanXmaSilencePad(0, 0, 0, true).fits);    // no output buffer
  SR_CHECK(!PlanXmaSilencePad(30, 2, 24, true).fits);  // offsets outside the ring
}
