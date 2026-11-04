#include "test_main.h"
#include "command_batch_policy.h"
using namespace superman_returns::native;
SR_TEST(command_batch_policy_preserves_default_and_bounds_overrides) {
  SR_CHECK_EQ(CommandBatchLimit(nullptr), 128u);
  SR_CHECK_EQ(CommandBatchLimit(""), 128u);
  SR_CHECK_EQ(CommandBatchLimit(nullptr, true), 32u);
  SR_CHECK_EQ(CommandBatchLimit("", true), 32u);
  SR_CHECK_EQ(CommandBatchLimit("32"), 32u);
  SR_CHECK_EQ(CommandBatchLimit("128"), 128u);
  SR_CHECK_EQ(CommandBatchLimit("256"), 256u);
  for (const char* invalid : {"0", "-1", "64", "257", "128garbage", " 128"})
    { SR_CHECK_EQ(CommandBatchLimit(invalid), 128u);
      SR_CHECK_EQ(CommandBatchLimit(invalid, true), 32u); }
}
