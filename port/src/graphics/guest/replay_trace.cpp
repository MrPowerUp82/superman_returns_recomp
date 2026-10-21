#include "replay_trace.h"
#include <locale>
#include <ostream>
namespace superman_returns::graphics::guest {
ReplayTraceWriter::ReplayTraceWriter(std::ostream& output,uint64_t clock_ns,int64_t utc_ns)
    :output_(output),clock_anchor_ns_(clock_ns),clock_anchor_utc_ns_(utc_ns) {
  output_.imbue(std::locale::classic());
  output_<<"schema,kind,sequence,closing_swap,api,path,shards,scheduling,commands,draws,wall_ns,prepare_ns,parallel_ns,join_wait_ns,direct_decode_ns,direct_arena_ns,dispatch_ns,shard,begin_ns,end_ns,arena_ns,decode_ns,clock_anchor_ns,clock_anchor_utc_ns\n";
}
bool ReplayTraceWriter::good() const {return output_.good();}
bool ReplayTraceWriter::Write(const ReplayBatchSample& sample) {
  if(!good())return false;
  const auto& options=sample.options;
  auto prefix=[&](const char* kind) {
    output_<<"1,"<<kind<<','<<sample.sequence<<','<<sample.closing_swap<<",vulkan,"
        <<(options.prepared?"prepared":"direct")<<','<<options.shards<<','
        <<(options.scheduling==ReplayScheduling::kInterleaved?"interleaved":"contiguous")<<',';
  };
  auto suffix=[&] {output_<<','<<clock_anchor_ns_<<','<<clock_anchor_utc_ns_<<'\n';};
  prefix("batch");output_<<sample.commands<<','<<sample.draws<<',';
  if(options.prepared) {
    const auto& p=sample.prepared;
    output_<<p.wall_ns<<','<<p.prepare_ns<<','<<p.parallel_ns<<','<<p.join_wait_ns
        <<",,,"<<p.dispatch_ns;
  } else output_<<",,,,"<<sample.direct_decode_ns<<','<<sample.direct_arena_ns<<',';
  output_<<",-1,,,,";suffix();
  if(options.prepared)for(unsigned i=0;i<sample.prepared.shard_count && i<8;++i) {
    const auto& p=sample.prepared.shards[i];prefix("shard");
    output_<<p.commands<<','<<p.draws<<",,,,,,,,"<<i<<','<<p.begin_ns<<','<<p.end_ns<<','
        <<p.arena_ns<<','<<p.decode_ns;suffix();
  }
  return good();
}
} // namespace superman_returns::graphics::guest
