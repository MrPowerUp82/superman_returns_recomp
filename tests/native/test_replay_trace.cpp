#include "../../port/src/graphics/guest/replay_trace.h"
#include "test_main.h"
#include <algorithm>
#include <sstream>
using namespace superman_returns::graphics::guest;
namespace {
std::vector<std::string> Fields(const std::string& row) {
  std::istringstream input(row);std::vector<std::string> result;std::string value;
  while(std::getline(input,value,','))result.push_back(value);
  return result;
}
}
SR_TEST(replay_trace_v1_has_one_batch_and_n_shards) {
  std::ostringstream output;ReplayTraceWriter writer(output,100,1000);
  ReplayBatchSample sample;sample.sequence=7;sample.closing_swap=11;sample.commands=3;sample.draws=2;
  sample.options.prepared=true;sample.options.shards=2;sample.options.scheduling=ReplayScheduling::kInterleaved;
  sample.prepared.wall_ns=50;sample.prepared.prepare_ns=10;sample.prepared.parallel_ns=30;
  sample.prepared.join_wait_ns=5;sample.prepared.dispatch_ns=110;sample.prepared.shard_count=2;
  sample.prepared.shards[0]={111,130,2,8,2,1};sample.prepared.shards[1]={112,135,3,9,1,1};
  SR_CHECK(writer.Write(sample));SR_CHECK(writer.good());
  std::istringstream lines(output.str());std::string row;
  std::getline(lines,row);
  SR_CHECK(row=="schema,kind,sequence,closing_swap,api,path,shards,scheduling,commands,draws,wall_ns,prepare_ns,parallel_ns,join_wait_ns,direct_decode_ns,direct_arena_ns,dispatch_ns,shard,begin_ns,end_ns,arena_ns,decode_ns,clock_anchor_ns,clock_anchor_utc_ns");
  std::getline(lines,row);auto fields=Fields(row);SR_CHECK_EQ(fields.size(),24u);
  SR_CHECK(fields[1]=="batch");SR_CHECK(fields[2]=="7");SR_CHECK(fields[3]=="11");
  SR_CHECK(fields[5]=="prepared");SR_CHECK(fields[7]=="interleaved");SR_CHECK(fields[10]=="50");
  SR_CHECK(fields[14].empty());SR_CHECK(fields[17]=="-1");
  for(unsigned s=0;s<2;++s) {
    std::getline(lines,row);fields=Fields(row);SR_CHECK_EQ(fields.size(),24u);
    SR_CHECK(fields[1]=="shard");SR_CHECK(fields[17]==std::to_string(s));
    SR_CHECK(fields[10].empty());SR_CHECK(fields[22]=="100");SR_CHECK(fields[23]=="1000");
  }
  SR_CHECK(!std::getline(lines,row));
}
SR_TEST(replay_trace_direct_has_no_fake_shards) {
  std::ostringstream output;ReplayTraceWriter writer(output,100,1000);
  ReplayBatchSample sample;sample.sequence=1;sample.direct_decode_ns=25;sample.direct_arena_ns=3;
  SR_CHECK(writer.Write(sample));std::istringstream lines(output.str());std::string row;
  std::getline(lines,row);std::getline(lines,row);const auto fields=Fields(row);
  SR_CHECK_EQ(fields.size(),24u);SR_CHECK(fields[5]=="direct");
  SR_CHECK(fields[10].empty());SR_CHECK(fields[16].empty());
  SR_CHECK(fields[14]=="25");SR_CHECK(fields[15]=="3");SR_CHECK(!std::getline(lines,row));
}
SR_TEST(replay_trace_reports_stream_failure) {
  std::ostream output(nullptr);ReplayTraceWriter writer(output,100,1000);
  SR_CHECK(!writer.Write(ReplayBatchSample{}));SR_CHECK(!writer.good());
}
