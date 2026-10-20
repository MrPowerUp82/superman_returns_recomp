#include "test_main.h"
#include "../../tools/rexglue-write-tracking/src/write_tracking_state.h"
#include "texture_write_proof.h"
#include <array>
#include <limits>

using sr::tracking::WriteTrackingState;
using namespace superman_returns::native;
namespace sr::tracking {
struct WriteTrackingTestAccess {
  static void ExhaustEpoch(WriteTrackingState& state) { state.sequence_ = UINT64_MAX; }
  static void ExhaustGeneration(WriteTrackingState& state) { state.generation_ = UINT64_MAX; }
  static size_t Allocations(const WriteTrackingState& state) { return state.allocations_.size(); }
};
}

SR_TEST(tracking_reallocation_rejects_old_identity) {
  WriteTrackingState state;
  const auto old = state.Allocate(0x1000, 0x2000);
  SR_CHECK(old != 0);
  SR_CHECK(state.MarkCovered({0x1000, 0x1000}));
  const std::array before{state.Sample({0x1000, 0x1000})};
  SR_CHECK(SameCoveredContent(before, before));
  state.Release(0x1000, 0x2000);
  const auto next = state.Allocate(0x1000, 0x2000);
  SR_CHECK(next != old);
  SR_CHECK(state.MarkCovered({0x1000, 0x1000}));
  const std::array after{state.Sample({0x1000, 0x1000})};
  SR_CHECK(!SameCoveredContent(before, after));
}

SR_TEST(tracking_all_ranges_required) {
  WriteTrackingState state;
  state.Allocate(0x1000, 0x4000);
  SR_CHECK(state.MarkCovered({0x1000, 0x1000}));
  const std::array stamps{state.Sample({0x1000, 0x1000}), state.Sample({0x4000, 0x100})};
  SR_CHECK(stamps[0].coverage == RexTrackingCoverage::Complete);
  SR_CHECK(stamps[1].coverage == RexTrackingCoverage::Unknown);
  SR_CHECK(!SameCoveredContent(stamps, stamps));
  SR_CHECK(state.MarkCovered({0x4000, 0x100}));
  const std::array covered{state.Sample({0x1000, 0x1000}), state.Sample({0x4000, 0x100})};
  SR_CHECK(SameCoveredContent(covered, covered));
}

SR_TEST(tracking_active_writer_and_end_do_not_restore_coverage) {
  WriteTrackingState state;
  state.Allocate(0x1000, 0x1000);
  SR_CHECK(state.MarkCovered({0x1000, 0x1000}));
  const auto epoch = state.Sample({0x1000, 1}).invalidation_epoch;
  state.BeginWrite(0x1000, 1);
  state.BeginWrite(0x1001, 1);
  const auto during = state.Sample({0x1000, 1});
  SR_CHECK_EQ(during.active_writers, 2u);
  SR_CHECK(during.invalidation_epoch > epoch);
  SR_CHECK(during.coverage == RexTrackingCoverage::Invalidated);
  state.EndWrite(0x1000, 1);
  SR_CHECK_EQ(state.Sample({0x1000, 1}).active_writers, 1u);
  state.EndWrite(0x1001, 1);
  SR_CHECK_EQ(state.Sample({0x1000, 1}).active_writers, 0u);
  SR_CHECK(!state.MarkCovered({0x1000, 1}));
}

SR_TEST(tracking_domain_bounds_and_shared_pages) {
  WriteTrackingState state;
  SR_CHECK_EQ(state.Allocate(0x1fffffff, 1), 1u);
  SR_CHECK(state.MarkCovered({0x1fffffff, 1}));
  SR_CHECK_EQ(state.Allocate(0x20000000, 1), 0u);
  SR_CHECK_EQ(state.Allocate(0xfffffff0, 0x40), 0u);
  SR_CHECK_EQ(state.Allocate(0x1000, 0), 0u);
  state.Allocate(0x1000, 0x200);  // Two views sharing a host page of one allocation.
  SR_CHECK(state.MarkCovered({0x1000, 0x100}));
  SR_CHECK(state.MarkCovered({0x1100, 0x100}));
  state.Invalidate(0x1101, 1);
  SR_CHECK(state.Sample({0x1000, 1}).coverage == RexTrackingCoverage::Invalidated);
  SR_CHECK(state.Sample({0x1100, 1}).coverage == RexTrackingCoverage::Invalidated);
}

SR_TEST(tracking_new_neighbor_allocation_invalidates_existing_page_proof) {
  WriteTrackingState state;
  state.Allocate(0x1000, 0x100);
  SR_CHECK(state.MarkCovered({0x1000, 0x100}));
  state.Allocate(0x1100, 0x100);
  SR_CHECK(state.Sample({0x1000, 1}).coverage == RexTrackingCoverage::Invalidated);
  SR_CHECK(!state.MarkCovered({0x1000, 1}));
}

SR_TEST(tracking_interval_epoch_summary_cannot_hide_a_new_write) {
  WriteTrackingState state;
  state.Allocate(0x1000, 0x2000);
  state.Invalidate(0x1000, 1);
  const auto old_epoch = state.Sample({0x1000, 0x2000}).invalidation_epoch;
  state.Invalidate(0x2000, 1);
  SR_CHECK(state.Sample({0x1000, 0x2000}).invalidation_epoch > old_epoch);
}

SR_TEST(tracking_reset_and_counter_overflow_fail_closed) {
  WriteTrackingState state;
  const auto old = state.Allocate(0x1000, 0x1000);
  SR_CHECK(state.MarkCovered({0x1000, 1}));
  state.Reset();
  SR_CHECK(state.Sample({0x1000, 1}).coverage != RexTrackingCoverage::Complete);
  SR_CHECK(state.Allocate(0x1000, 0x1000) != old);
  SR_CHECK(state.MarkCovered({0x1000, 1}));
  sr::tracking::WriteTrackingTestAccess::ExhaustEpoch(state);
  state.Invalidate(0x1000, 1);
  SR_CHECK(!state.MarkCovered({0x1000, 1}));
  SR_CHECK(state.Sample({0x1000, 1}).coverage != RexTrackingCoverage::Complete);
  state.Reset();
  SR_CHECK_EQ(state.Allocate(0x1000, 0x1000), 0u);
  WriteTrackingState other;
  sr::tracking::WriteTrackingTestAccess::ExhaustGeneration(other);
  SR_CHECK_EQ(other.Allocate(0x1000, 0x1000), 0u);
}

SR_TEST(tracking_unbalanced_writer_and_overlapping_allocation_fail_closed) {
  WriteTrackingState state;
  state.Allocate(0x1000, 0x1000);
  SR_CHECK(state.MarkCovered({0x1000, 1}));
  SR_CHECK_EQ(state.Allocate(0x1800, 0x1000), 0u);
  SR_CHECK(state.Sample({0x1000, 1}).coverage != RexTrackingCoverage::Complete);
  state.EndWrite(0x1000, 1);
  SR_CHECK(!state.MarkCovered({0x1000, 1}));
}

SR_TEST(tracking_allocation_registry_does_not_grow_with_reuse) {
  WriteTrackingState state;
  for (unsigned i = 0; i < 1000; ++i) {
    SR_CHECK(state.Allocate(0x1000, 0x1000) != 0);
    state.Release(0x1000, 0x1000);
  }
  SR_CHECK_EQ(sr::tracking::WriteTrackingTestAccess::Allocations(state), 0u);
}
