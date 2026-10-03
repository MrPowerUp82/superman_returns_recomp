#include "smoke_options.h"
#include "test_main.h"
using namespace superman_returns::graphics::vulkan;
SR_TEST(cli_rejects_bad_uuid_and_zero_frame_limit) {
  SmokeOptions o;
  std::string e;
  std::vector<std::string> a{"--gpu-uuid=bad"};
  SR_CHECK(!ParseOptions(a, o, e));
  a = {"--frames=0"};
  SR_CHECK(!ParseOptions(a, o, e));
  a = {"--unknown"};
  SR_CHECK(!ParseOptions(a, o, e));
  a = {"--frames=120", "--gpu-uuid=00000000000000000000000000000000",
       "--validation"};
  SR_CHECK(ParseOptions(a, o, e));
  SR_CHECK_EQ(o.frames, 120);
  SR_CHECK(o.validation);
}
