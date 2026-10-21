#pragma once
#include "render_packet.h"
#include "replay_options.h"
#include <condition_variable>
#include <thread>

namespace superman_returns::graphics::guest {
struct ReplayResult {
  RenderPacket packet;
  std::string error;
  uint64_t vs_version=0,ps_version=0;
  bool ok=false;
};
// Include the failed entry so the consumer can emit its normal error, but
// never execute a later packet that happened to decode successfully.
size_t ReplayExecutionLimit(std::span<const ReplayResult>) noexcept;
struct ReplayShardProbe {
  uint64_t begin_ns=0,end_ns=0,arena_ns=0,decode_ns=0;
  size_t commands=0,draws=0;
};
struct ReplayBatchProbe {
  ReplayProbe stages; // decode_ns is CPU sum across shards, not wall time
  uint64_t arena_ns=0,prepare_ns=0,wall_ns=0;
  uint64_t dispatch_ns=0,parallel_ns=0,join_wait_ns=0;
  unsigned shard_count=0;
  std::array<ReplayShardProbe,8> shards{};
};
// Single caller. Decode joins every shard before returning ordered results.
class ParallelReplayDecoder {
public:
  explicit ParallelReplayDecoder(unsigned threads,
      ReplayScheduling scheduling=ReplayScheduling::kContiguous);
  ~ParallelReplayDecoder();
  ParallelReplayDecoder(const ParallelReplayDecoder&)=delete;
  ParallelReplayDecoder& operator=(const ParallelReplayDecoder&)=delete;
  std::vector<ReplayResult> Decode(std::shared_ptr<const WorkBatch>,native::Pm4Mirror&,
      ReplayBatchProbe* =nullptr,bool replay_storage=true,std::shared_ptr<DrawPacketPool> ={});
private:
  void Worker(size_t shard);
  void DecodeShard(size_t shard) noexcept;
  struct Entry {PreparedReplayCommand prepared;ReplayResult result;bool ready=false;};
  struct ShardProbe {ReplayProbe stages;ReplayShardProbe detail;};
  std::mutex mutex_;
  std::condition_variable work_,done_;
  std::vector<std::thread> threads_;
  uint64_t epoch_=0;
  size_t pending_=0,shards_=1;
  ReplayScheduling scheduling_=ReplayScheduling::kContiguous;
  bool stop_=false,replay_storage_=true,profile_=false;
  std::shared_ptr<const WorkBatch> batch_;
  std::shared_ptr<DrawPacketPool> pool_;
  std::vector<Entry> entries_;
  std::array<ShardProbe,8> probes_{};
#ifdef SR_REPLAY_TEST_HOOKS
  friend struct ReplayTestAccess;
  void (*before_entry_)(size_t)=nullptr;
#endif
};
} // namespace superman_returns::graphics::guest
