#include "../../port/src/benchmark_input.h"
#include "test_main.h"
using superman_returns::BenchmarkInput;
using superman_returns::BenchmarkPadState;
SR_TEST(benchmark_input_holds_from_first_observation_and_releases_on_deadline) {
  BenchmarkInput input;
  SR_CHECK(input.Sample(0)==BenchmarkPadState{});
  SR_CHECK(input.Update("1 500 16 0\n",1000));
  SR_CHECK_EQ(input.Sample(1000).buttons,16);
  SR_CHECK_EQ(input.Sample(1499).buttons,16);
  SR_CHECK(input.Sample(1500)==BenchmarkPadState{});
  SR_CHECK(!input.Update("1 500 16 0",1600));
  SR_CHECK(input.Sample(1600)==BenchmarkPadState{});
  SR_CHECK(input.Update("2 20000 0 32767",1600));
  SR_CHECK_EQ(input.Sample(2000).forward,32767);
  SR_CHECK(input.Sample(21600)==BenchmarkPadState{});
}
SR_TEST(benchmark_input_rejects_partial_invalid_and_unbounded_commands) {
  BenchmarkInput input;
  for(auto command:{"", "1 500 16", "1 500 16-1", "-1 500 16 0", "0 500 16 0", "1 0 16 0", "1 30001 16 0", "1 500 65536 0", "1 500 16 -32769", "1 500 16 32768", "1 500 16 0 extra", "18446744073709551616 500 16 0"})
    SR_CHECK(!input.Update(command,100));
  SR_CHECK(input.Sample(100)==BenchmarkPadState{});
  SR_CHECK(input.Update("1 500 4096 -32768",100));
  SR_CHECK(!input.Update("2 500 broken 0",200));
  SR_CHECK_EQ(input.Sample(200).buttons,4096);
  SR_CHECK_EQ(input.Sample(200).forward,-32768);
  SR_CHECK(input.Sample(600)==BenchmarkPadState{});
}
SR_TEST(benchmark_input_new_command_replaces_old_and_clock_wrap_does_not_stick) {
  BenchmarkInput input;
  SR_CHECK(input.Update("5 30000 4096 100",100));
  SR_CHECK(!input.Update("4 500 16 0",101));
  SR_CHECK(input.Update("6 1 16 0",200));
  SR_CHECK_EQ(input.Sample(200).buttons,16);
  SR_CHECK(input.Sample(201)==BenchmarkPadState{});
  SR_CHECK(input.Update("7 10 16 0",UINT64_MAX-5));
  SR_CHECK_EQ(input.Sample(UINT64_MAX).buttons,16);
  SR_CHECK(input.Sample(0)==BenchmarkPadState{});
}
