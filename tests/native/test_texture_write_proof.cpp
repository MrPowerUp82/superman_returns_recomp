#include "test_main.h"
#include "texture_write_proof.h"
#include <array>

using namespace superman_returns::native;

SR_TEST(tracking_missing_or_changed_stamp_rejects_proof) {
  const std::array original{RexTrackingStamp{7, 8, 9, 0, RexTrackingCoverage::Complete}};
  SR_CHECK(SameCoveredContent(original, original));
  SR_CHECK(!SameCoveredContent({}, {}));
  SR_CHECK(!SameCoveredContent(original, {}));
  auto changed = original;
  ++changed[0].allocation_generation;
  SR_CHECK(!SameCoveredContent(original, changed));
  changed = original; ++changed[0].coverage_generation;
  SR_CHECK(!SameCoveredContent(original, changed));
  changed = original; ++changed[0].invalidation_epoch;
  SR_CHECK(!SameCoveredContent(original, changed));
  changed = original; changed[0].active_writers = 1;
  SR_CHECK(!SameCoveredContent(original, changed));
  SR_CHECK(!SameCoveredContent(changed, changed));
  changed = original; changed[0].coverage = RexTrackingCoverage::Unknown;
  SR_CHECK(!SameCoveredContent(changed, changed));
  changed[0].coverage = RexTrackingCoverage::Invalidated;
  SR_CHECK(!SameCoveredContent(changed, changed));
  changed = original; changed[0].allocation_generation = 0;
  SR_CHECK(!SameCoveredContent(changed, changed));
  changed = original; changed[0].coverage_generation = 0;
  SR_CHECK(!SameCoveredContent(changed, changed));
}
