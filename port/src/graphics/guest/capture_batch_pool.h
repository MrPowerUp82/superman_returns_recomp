#pragma once
#include "captured_batch.h"
#include <mutex>

namespace superman_returns::graphics::guest {
// A frozen batch may outlive the frontend/replay worker. Its deleter owns
// this pool, not the renderer, so late release never calls a destroyed owner.
class CaptureBatchPool : public std::enable_shared_from_this<CaptureBatchPool> {
public:
  CaptureBatchPool() {free_.reserve(kRetainedBatches);}
  std::unique_ptr<WorkBatch> Acquire() {
    std::lock_guard lock(mutex_);
    if(free_.empty()) return std::make_unique<WorkBatch>();
    auto batch=std::move(free_.back());free_.pop_back();return batch;
  }
  std::shared_ptr<const WorkBatch> Freeze(std::unique_ptr<WorkBatch> batch) {
    if(!batch) return {};
    auto pool=shared_from_this();
    return {batch.release(),[pool=std::move(pool)](const WorkBatch* immutable) {
      std::unique_ptr<WorkBatch> recycled(const_cast<WorkBatch*>(immutable));
      recycled->Clear();
      std::lock_guard lock(pool->mutex_);
      if(pool->free_.size()<kRetainedBatches) pool->free_.push_back(std::move(recycled));
    }};
  }
private:
  static constexpr size_t kRetainedBatches=128;
  std::mutex mutex_;
  std::vector<std::unique_ptr<WorkBatch>> free_;
};
}
