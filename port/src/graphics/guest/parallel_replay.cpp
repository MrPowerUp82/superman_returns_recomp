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
size_t ReplayExecutionLimit(std::span<const ReplayResult> results) noexcept {
  for(size_t i=0;i<results.size();++i)if(!results[i].ok)return i+1;
  return results.size();
}
ParallelReplayDecoder::ParallelReplayDecoder(unsigned count,ReplayScheduling scheduling)
    :scheduling_(scheduling) {
  shards_=std::clamp(count,1u,8u);
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
  auto& detail=probes_[shard].detail;
  if(profile_)detail.begin_ns=Now();
  const bool interleaved=scheduling_==ReplayScheduling::kInterleaved;
  const size_t begin=interleaved?shard:entries_.size()*shard/shards_;
  const size_t end=interleaved?entries_.size():entries_.size()*(shard+1)/shards_;
  const size_t step=interleaved?shards_:1;
  try {
    size_t draws=0,commands=0;
    for(size_t i=begin;i<end;i+=step)if(entries_[i].ready) {
      ++commands;draws+=IsDraw(batch_->cmds[i]);
    }
    if(profile_){detail.commands=commands;detail.draws=draws;}
    const auto started=profile_?Now():0;
    std::optional<DrawPacketArena> arena;
    if(draws)arena.emplace(draws,replay_storage_,pool_);
    if(profile_)detail.arena_ns+=Now()-started;
    for(size_t i=begin;i<end;i+=step) {
      auto& entry=entries_[i];if(!entry.ready)continue;
#ifdef SR_REPLAY_TEST_HOOKS
      if(before_entry_)before_entry_(i);
#endif
      entry.result.ok=DecodePreparedRenderPacket(*batch_,batch_->cmds[i],entry.prepared,
          entry.result.packet,entry.result.error,arena && IsDraw(batch_->cmds[i])?arena->Next():DrawPacketOwner{},
          profile_?&probes_[shard].stages:nullptr);
    }
  } catch(const std::exception& ex) {
    // Fail unfinished commands without stranding the caller at the join.
    for(size_t i=begin;i<end;i+=step)if(entries_[i].ready && !entries_[i].result.ok) {
      try {entries_[i].result.error=ex.what();} catch(...) {}
    }
  } catch(...) {
    for(size_t i=begin;i<end;i+=step)if(entries_[i].ready && !entries_[i].result.ok) {
      try {entries_[i].result.error="Replay worker failed";} catch(...) {}
    }
  }
  if(profile_) {detail.decode_ns=probes_[shard].stages.decode_ns;detail.end_ns=Now();}
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
  uint64_t dispatch_ns=0,parallel_ns=0,join_wait_ns=0;
  if(count) {
    if(probe)dispatch_ns=Now();
    {std::lock_guard lock(mutex_);pending_=threads_.size();++epoch_;}
    work_.notify_all();DecodeShard(0);
    const auto join_started=probe?Now():0;
    {std::unique_lock lock(mutex_);done_.wait(lock,[&]{return pending_==0;});}
    if(probe) {const auto finished=Now();parallel_ns=finished-dispatch_ns;join_wait_ns=finished-join_started;}
  }
  std::vector<ReplayResult> results;results.reserve(count);
  for(auto& entry:entries_)results.push_back(std::move(entry.result));
  entries_.clear();batch_.reset();pool_.reset();
  if(probe) {
    *probe={};probe->wall_ns=Now()-started;probe->prepare_ns=prepare_ns;
    probe->dispatch_ns=dispatch_ns;probe->parallel_ns=parallel_ns;probe->join_wait_ns=join_wait_ns;
    probe->shard_count=unsigned(shards_);
    for(size_t i=0;i<shards_;++i) {
      const auto& p=probes_[i];probe->shards[i]=p.detail;
      probe->arena_ns+=p.detail.arena_ns;probe->stages.capture_ns+=p.stages.capture_ns;
      probe->stages.pm4_ns+=p.stages.pm4_ns;probe->stages.decode_ns+=p.stages.decode_ns;
    }
  }
  return results;
}
} // namespace superman_returns::graphics::guest
