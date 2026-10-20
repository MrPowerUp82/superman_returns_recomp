#pragma once
#include "render_packet.h"
#include <condition_variable>
#include <thread>

namespace superman_returns::graphics::guest {
struct ReplayResult {
  RenderPacket packet;
  std::string error;
  uint64_t vs_version=0,ps_version=0;
  bool ok=false;
};
struct ReplayBatchProbe {
  ReplayProbe stages; // decode_ns is CPU sum across shards, not wall time
  uint64_t arena_ns=0,prepare_ns=0,wall_ns=0;
};
// Single caller. Decode joins every shard before returning ordered results.
class ParallelReplayDecoder {
public:
  explicit ParallelReplayDecoder(unsigned threads);
  ~ParallelReplayDecoder();
  ParallelReplayDecoder(const ParallelReplayDecoder&)=delete;
  ParallelReplayDecoder& operator=(const ParallelReplayDecoder&)=delete;
  std::vector<ReplayResult> Decode(std::shared_ptr<const WorkBatch>,native::Pm4Mirror&,
      ReplayBatchProbe* =nullptr,bool replay_storage=true,std::shared_ptr<DrawPacketPool> ={});
private:
  void Worker(size_t shard);
  void DecodeShard(size_t shard) noexcept;
  struct Entry {PreparedReplayCommand prepared;ReplayResult result;bool ready=false;};
  struct ShardProbe {ReplayProbe stages;uint64_t arena_ns=0;};
  std::mutex mutex_;
  std::condition_variable work_,done_;
  std::vector<std::thread> threads_;
  uint64_t epoch_=0;
  size_t pending_=0,shards_=1;
  bool stop_=false,replay_storage_=true,profile_=false;
  std::shared_ptr<const WorkBatch> batch_;
  std::shared_ptr<DrawPacketPool> pool_;
  std::vector<Entry> entries_;
  std::array<ShardProbe,4> probes_{};
};
} // namespace superman_returns::graphics::guest
