#include "test_main.h"
#include "../../tools/rexglue-write-tracking/src/write_tracking_state.h"
#include "../../port/src/native_renderer/resource_write_tracking.h"
#include <memory>
#include <vector>

using sr::tracking::WriteTrackingState;
using superman_returns::native::ResourceWriteTracker;
namespace {
struct Mirror {
  std::vector<uint64_t> pages = std::vector<uint64_t>(WriteTrackingState::kPhysicalSize / WriteTrackingState::kPageSize);
  uint64_t domain = 1;
  RexTrackingFastView view{1, uint32_t(pages.size()), pages.data(), &domain};
};
bool Holds(const Mirror& m, RexTrackingRange r, const RexTrackingStamp& s, uint64_t domain) {
  return ResourceWriteTracker::FastStampHoldsIn(&m.view, r, s.invalidation_epoch, domain);
}
}  // namespace

SR_TEST(tracking_fast_view_holds_until_a_page_of_the_range_is_invalidated) {
  auto m = std::make_unique<Mirror>();
  WriteTrackingState state;
  state.SetEpochMirror(m->pages.data(), &m->domain);
  state.Allocate(0x10000, 0x4000);
  const RexTrackingRange range{0x10100, 0x1F00};  // pages 0x10 and 0x11
  SR_CHECK(state.MarkCovered(range));
  const auto stamp = state.Sample(range);
  const uint64_t domain = m->domain;
  SR_CHECK(stamp.coverage == RexTrackingCoverage::Complete);
  SR_CHECK(Holds(*m, range, stamp, domain));
  state.Invalidate(0x13000, 0x100);  // a page outside the range
  SR_CHECK(Holds(*m, range, stamp, domain));
  state.Invalidate(0x11000, 4);  // second page of the range
  SR_CHECK(!Holds(*m, range, stamp, domain));
}

SR_TEST(tracking_fast_view_fails_on_writers_reset_and_domain_loss) {
  auto m = std::make_unique<Mirror>();
  // Any invalidation retires the whole allocation until it is reallocated,
  // so each case starts from a fresh allocation.
  auto fresh = [&](WriteTrackingState& state, uint32_t address, RexTrackingStamp& stamp, uint64_t& domain) {
    state.SetEpochMirror(m->pages.data(), &m->domain);
    state.Allocate(address, 0x2000);
    SR_CHECK(state.MarkCovered({address, 0x1000}));
    stamp = state.Sample({address, 0x1000});
    domain = m->domain;
    SR_CHECK(Holds(*m, {address, 0x1000}, stamp, domain));
  };
  RexTrackingStamp stamp{};
  uint64_t domain = 0;
  WriteTrackingState writer;
  fresh(writer, 0x20000, stamp, domain);
  writer.BeginWrite(0x20000, 8);  // a host producer starts on the page
  SR_CHECK(!Holds(*m, {0x20000, 0x1000}, stamp, domain));
  writer.EndWrite(0x20000, 8);

  WriteTrackingState released;
  fresh(released, 0x40000, stamp, domain);
  released.Release(0x40000, 0x2000);  // the bytes change owner
  SR_CHECK(!Holds(*m, {0x40000, 0x1000}, stamp, domain));

  WriteTrackingState lost;
  fresh(lost, 0x60000, stamp, domain);
  lost.Invalidate(0, 0);  // invalid range: the whole domain is lost
  SR_CHECK(!Holds(*m, {0x60000, 0x1000}, stamp, domain));

  WriteTrackingState reset;
  fresh(reset, 0x80000, stamp, domain);
  reset.Reset();
  SR_CHECK(!Holds(*m, {0x80000, 0x1000}, stamp, domain));
  SR_CHECK(!ResourceWriteTracker::FastStampHoldsIn(nullptr, {0x80000, 0x1000}, 0, domain));
}
