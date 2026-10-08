#include "texture_content.h"
#include "test_main.h"
#include <array>

using namespace superman_returns::native;
using superman_returns::graphics::guest::TextureRange;
SR_TEST(clean_watch_scan_reuses_only_the_same_frame_completion_and_baseline) {
  CleanTextureWatchScan cache;
  uint32_t scans=0; bool reused=false;
  auto clean=[&] {++scans;return false;};
  SR_CHECK(!cache.Written(1,0,1,clean,reused)); SR_CHECK(!reused);
  SR_CHECK(!cache.Written(1,0,1,clean,reused)); SR_CHECK(reused);
  SR_CHECK(!cache.Written(2,0,1,clean,reused)); SR_CHECK(!reused);
  SR_CHECK(!cache.Written(2,1,1,clean,reused)); SR_CHECK(!reused);
  SR_CHECK(!cache.Written(2,1,2,clean,reused)); SR_CHECK(!reused);
  SR_CHECK_EQ(scans,4u);
}
SR_TEST(clean_watch_scan_does_not_memoize_dirty_results) {
  CleanTextureWatchScan cache;
  uint32_t scans=0; bool reused=false;
  auto dirty=[&] {++scans;return true;};
  SR_CHECK(cache.Written(1,0,1,dirty,reused)); SR_CHECK(!reused);
  SR_CHECK(cache.Written(1,0,1,dirty,reused)); SR_CHECK(!reused);
  SR_CHECK_EQ(scans,2u);
}
SR_TEST(clean_watch_scan_rechecks_a_write_completed_during_the_previous_scan) {
  CleanTextureWatchScan cache;
  uint64_t completed=0; bool reused=false;
  auto overlapping=[&] {++completed;return false;};
  SR_CHECK(!cache.Written(1,completed,1,overlapping,reused));
  SR_CHECK(cache.Written(1,completed,1,[] {return true;},reused));
  SR_CHECK(!reused);
}
SR_TEST(texture_content_watch_detects_a_write_confined_to_the_mip_tail) {
  const std::array<TextureRange, 2> ranges{{{0x1000, 0x1000}, {0x9000, 0x2000}}};
  auto written = [](uint32_t address, uint32_t length, uint32_t sequence) {
    return address <= 0xafffu && uint64_t(address) + length > 0xafffu && sequence < 8;
  };
  SR_CHECK(!TextureContentWritten(std::span(ranges).first(1), 7, written));
  SR_CHECK(TextureContentWritten(ranges, 7, written));
  SR_CHECK(!TextureContentWritten(ranges, 8, written));
}
