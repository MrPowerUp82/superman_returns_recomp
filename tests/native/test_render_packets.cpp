#include "../../port/src/graphics/guest/captured_batch.h"
#include "test_main.h"
#include <stdexcept>
using namespace superman_returns::graphics::guest;
SR_TEST(captured_memory_owns_bytes_after_source_reuse) {
  WorkBatch batch;
  batch.bytes = {1, 2, 3, 4, 5, 6};
  batch.ranges = {{0x1000, 4, 0}, {0x2000, 2, 4}};
  WorkCmd cmd;
  cmd.range_count = 2;
  CapturedMemory memory;
  std::string error;
  SR_CHECK(CapturedMemory::Capture(batch, cmd, memory, error));
  batch.Clear();
  SR_CHECK_EQ(memory.Read(0x1000, 4)[2], 3);
  SR_CHECK_EQ(memory.Read(0x2000, 2)[1], 6);
}
SR_TEST(capture_range_overflow_is_rejected) {
  WorkBatch batch;
  batch.bytes = {1, 2, 3, 4};
  WorkCmd cmd;
  cmd.range_count = 1;
  CapturedMemory memory;
  std::string error;
  batch.ranges = {{UINT32_MAX, 4, 0}};
  SR_CHECK(!CapturedMemory::Capture(batch, cmd, memory, error));
  batch.ranges = {{0x1000, 4, UINT32_MAX}};
  SR_CHECK(!CapturedMemory::Capture(batch, cmd, memory, error));
  cmd.range_first = UINT32_MAX;
  SR_CHECK(!CapturedMemory::Capture(batch, cmd, memory, error));
}
SR_TEST(capture_overlap_uses_latest_range_without_live_fallback) {
  WorkBatch batch;
  batch.bytes = {1, 2, 3, 4, 9, 8, 7, 6};
  batch.ranges = {{0x1000, 4, 0}, {0x1000, 4, 4}};
  WorkCmd cmd;
  cmd.range_count = 2;
  CapturedMemory memory;
  std::string error;
  SR_CHECK(CapturedMemory::Capture(batch, cmd, memory, error));
  SR_CHECK_EQ(memory.Read(0x1001, 2)[0], 8);
  bool rejected = false;
  try {
    memory.Read(0x1003, 2);
  } catch (const std::out_of_range &) {
    rejected = true;
  }
  SR_CHECK(rejected);
}
SR_TEST(capture_command_ranges_do_not_leak_other_draws) {
  WorkBatch batch;
  batch.bytes = {1, 2, 3, 4};
  batch.ranges = {{0x1000, 2, 0}, {0x1000, 2, 2}};
  WorkCmd cmd;
  cmd.range_count = 1;
  CapturedMemory first, second;
  std::string error;
  SR_CHECK(CapturedMemory::Capture(batch, cmd, first, error));
  cmd.range_first = 1;
  SR_CHECK(CapturedMemory::Capture(batch, cmd, second, error));
  SR_CHECK_EQ(first.Read(0x1000, 2)[0], 1);
  SR_CHECK_EQ(second.Read(0x1000, 2)[0], 3);
}
