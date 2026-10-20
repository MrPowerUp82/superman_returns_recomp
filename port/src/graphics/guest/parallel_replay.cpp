#include "parallel_replay.h"
#include <algorithm>
#include <chrono>
#include <optional>

namespace superman_returns::graphics::guest {
namespace {
uint64_t Now() {return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count());}
bool IsDraw(const WorkCmd& c) {
  return c.op==Op::kDraw || c.op==Op::kDrawIndexed || c.op==Op::kDrawInline;
}
}
ParallelReplayDecoder::ParallelReplayDecoder(unsigned count) {
  shards_=std::clamp(count,1u,4u);
  try {
    for(size_t i=1;i<shards_;++i)threads_.emplace_back([this,i]{Worker(i);});
  } catch(...) {
    {std::lock_guard lock(mutex_);stop_=true;}
    work_.notify_all();for(auto& thread:threads_)thread.join();throw;
  }
}
ParallelReplayDecoder::~ParallelReplayDecoder() {
  {std::lock_guard lock(mutex_);stop_=true;}
  work_.notify_all();for(auto& thread:threads_)thread.join();
}
void ParallelReplayDecoder::Worker(size_t shard) {
  uint64_t seen=0;
  for(;;) {
    {
      std::unique_lock lock(mutex_);
      work_.wait(lock,[&]{return stop_ || epoch_!=seen;});
      if(stop_)return;
      seen=epoch_;
    }
    DecodeShard(shard);
    {std::lock_guard lock(mutex_);--pending_;}
    done_.notify_one();
  }
}
void ParallelReplayDecoder::DecodeShard(size_t shard) noexcept {
  const size_t begin=entries_.size()*shard/shards_,end=entries_.size()*(shard+1)/shards_;
  try {
    size_t draws=0;
    for(size_t i=begin;i<end;++i)draws+=entries_[i].ready && IsDraw(batch_->cmds[i]);
    const auto started=profile_?Now():0;
    std::optional<DrawPacketArena> arena;
    if(draws)arena.emplace(draws,replay_storage_,pool_);
    if(profile_)probes_[shard].arena_ns+=Now()-started;
    for(size_t i=begin;i<end;++i) {
      auto& entry=entries_[i];if(!entry.ready)continue;
      entry.result.ok=DecodePreparedRenderPacket(*batch_,batch_->cmds[i],entry.prepared,
          entry.result.packet,entry.result.error,arena && IsDraw(batch_->cmds[i])?arena->Next():DrawPacketOwner{},
          profile_?&probes_[shard].stages:nullptr);
    }
  } catch(const std::exception& ex) {
    // Fail unfinished commands without stranding the caller at the join.
    for(size_t i=begin;i<end;++i)if(entries_[i].ready && !entries_[i].result.ok) {
      try {entries_[i].result.error=ex.what();} catch(...) {}
    }
  } catch(...) {
    for(size_t i=begin;i<end;++i)if(entries_[i].ready && !entries_[i].result.ok) {
      try {entries_[i].result.error="Replay worker failed";} catch(...) {}
    }
  }
}
std::vector<ReplayResult> ParallelReplayDecoder::Decode(std::shared_ptr<const WorkBatch> batch,
    native::Pm4Mirror& mirror,ReplayBatchProbe* probe,bool replay_storage,std::shared_ptr<DrawPacketPool> pool) {
  if(!batch)throw std::invalid_argument("Replay requires a frozen batch");
  const auto started=probe?Now():0;
  batch_=std::move(batch);pool_=std::move(pool);replay_storage_=replay_storage;profile_=probe!=nullptr;
  entries_.clear();entries_.resize(batch_->cmds.size());probes_={};
  const auto prepare_started=probe?Now():0;
  size_t count=0;
  for(const auto& cmd:batch_->cmds) {
    auto& entry=entries_[count++];
    entry.ready=PrepareReplayCommand(*batch_,cmd,mirror,entry.prepared,entry.result.error,batch_,
        probe?&probes_[0].stages:nullptr);
    entry.result.vs_version=entry.prepared.vs_version;entry.result.ps_version=entry.prepared.ps_version;
    if(!entry.ready)break;
  }
  entries_.resize(count);
  const auto prepare_ns=probe?Now()-prepare_started:0;
  if(count) {
    {std::lock_guard lock(mutex_);pending_=threads_.size();++epoch_;}
    work_.notify_all();DecodeShard(0);
    {std::unique_lock lock(mutex_);done_.wait(lock,[&]{return pending_==0;});}
  }
  std::vector<ReplayResult> results;results.reserve(count);
  for(auto& entry:entries_)results.push_back(std::move(entry.result));
  entries_.clear();batch_.reset();pool_.reset();
  if(probe) {
    *probe={};probe->wall_ns=Now()-started;probe->prepare_ns=prepare_ns;
    for(const auto& p:probes_) {
      probe->arena_ns+=p.arena_ns;probe->stages.capture_ns+=p.stages.capture_ns;
      probe->stages.pm4_ns+=p.stages.pm4_ns;probe->stages.decode_ns+=p.stages.decode_ns;
    }
  }
  return results;
}
} // namespace superman_returns::graphics::guest
