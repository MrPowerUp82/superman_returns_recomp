#include "test_main.h"
#include "../../tools/rexglue-write-tracking/src/write_tracking_state.h"
#include "../../port/src/native_renderer/resource_write_tracking.h"
#include <array>
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

namespace {
struct TextureMirror {
  Mirror mirror;
  WriteTrackingState state;
  // Separate allocations model a base image and its non-contiguous mip tail.
  std::array<RexTrackingRange, 2> ranges{{{0x10100, 0x1F00}, {0x30100, 0xF00}}};
  superman_returns::native::TextureUploadProof proof;
  TextureMirror() {
    state.SetEpochMirror(mirror.pages.data(), &mirror.domain);
    state.Allocate(0x10000, 0x2000);
    state.Allocate(0x30000, 0x1000);
    proof.watch = 1;
    proof.resource_version = 55;
    for (const auto range : ranges) {
      SR_CHECK(state.MarkCovered(range));
      proof.stamps.push_back(state.Sample(range));
    }
    proof.domain = mirror.domain;
  }
  bool Reuses(uint64_t version = 55) const {
    return ResourceWriteTracker::CanReuseTextureFastIn(&mirror.view, proof, ranges, version);
  }
};
}  // namespace

SR_TEST(texture_fast_proof_covers_every_mip_and_keeps_resource_identity) {
  auto t = std::make_unique<TextureMirror>();
  SR_CHECK(t->Reuses());
  SR_CHECK(!t->Reuses(0));
  SR_CHECK(!t->Reuses(56));
  t->state.Invalidate(0x11000, 4);
  SR_CHECK(!t->Reuses());

  t = std::make_unique<TextureMirror>();
  SR_CHECK(t->Reuses());
  t->state.BeginWrite(0x30100, 4);  // the second range cannot be omitted
  SR_CHECK(!t->Reuses());
  t->state.EndWrite(0x30100, 4);
  SR_CHECK(!t->Reuses());

  t = std::make_unique<TextureMirror>();
  t->state.Release(0x30000, 0x1000);
  t->state.Allocate(0x30000, 0x1000);
  SR_CHECK(!t->Reuses());
}

SR_TEST(texture_fast_proof_requires_a_complete_writer_free_upload_baseline) {
  auto t = std::make_unique<TextureMirror>();
  const auto original = t->proof;
  t->proof.watch = 0;
  SR_CHECK(!t->Reuses());
  t->proof = original;
  t->proof.stamps[1].active_writers = 1;
  SR_CHECK(!t->Reuses());
  for (const auto coverage : {RexTrackingCoverage::Unknown, RexTrackingCoverage::Invalidated}) {
    t->proof = original;
    t->proof.stamps[1].coverage = coverage;
    SR_CHECK(!t->Reuses());
  }
  t->proof = original;
  t->proof.stamps[1].allocation_generation = 0;
  SR_CHECK(!t->Reuses());
  t->proof = original;
  t->proof.stamps[1].coverage_generation = 0;
  SR_CHECK(!t->Reuses());
  t->proof = original;
  t->proof.stamps.pop_back();
  SR_CHECK(!t->Reuses());
  t->proof.stamps.clear();
  SR_CHECK(!t->Reuses());
}

SR_TEST(texture_fast_proof_rejects_missing_view_domain_changes_and_invalid_ranges) {
  auto t = std::make_unique<TextureMirror>();
  SR_CHECK(!ResourceWriteTracker::CanReuseTextureFastIn(nullptr, t->proof, t->ranges, 55));
  SR_CHECK(!ResourceWriteTracker::CanReuseTextureFastIn(&t->mirror.view, t->proof, {}, 55));
  const uint64_t domain = t->proof.domain;
  t->proof.domain = 0;
  SR_CHECK(!t->Reuses());
  t->proof.domain = domain;
  ++t->mirror.domain;
  SR_CHECK(!t->Reuses());
  t->mirror.domain = domain;
  const auto ranges = t->ranges;
  t->ranges[1].length = 0;
  SR_CHECK(!t->Reuses());
  t->ranges = ranges;
  t->ranges[1] = {0x1FFFFF00, 0x200}; // crosses the physical mapping boundary
  SR_CHECK(!t->Reuses());
  t->ranges = ranges;
  t->state.Invalidate(0, 0);
  SR_CHECK(!t->Reuses());
}
