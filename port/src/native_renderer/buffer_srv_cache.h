#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <unordered_map>

namespace superman_returns::native {
struct BufferSrvLease {
  uint64_t generation;
  uint32_t index;
};
// Owns leases, not GPU descriptors. Every returned lease must be retired by
// the renderer with a fence that covers all draws which may reference it.
class BufferSrvCache {
 public:
  explicit BufferSrvCache(size_t capacity):capacity_(capacity) {}
  BufferSrvCache(const BufferSrvCache&)=delete;
  BufferSrvCache& operator=(const BufferSrvCache&)=delete;
  uint32_t Find(uint64_t generation) {
    auto found=by_generation_.find(generation);
    if(found==by_generation_.end()) return 0;
    leases_.splice(leases_.begin(),leases_,found->second);
    return found->second->index;
  }
  std::optional<BufferSrvLease> Insert(uint64_t generation,uint32_t index) {
    if(!index) return std::nullopt;
    if(!capacity_ || !generation) return BufferSrvLease{generation,index};
    auto found=by_generation_.find(generation);
    if(found!=by_generation_.end()) {
      const auto old=*found->second;
      found->second->index=index;
      leases_.splice(leases_.begin(),leases_,found->second);
      return old.index==index?std::nullopt:std::optional(old);
    }
    leases_.push_front({generation,index});
    by_generation_.emplace(generation,leases_.begin());
    if(leases_.size()<=capacity_) return std::nullopt;
    const auto old=leases_.back();
    by_generation_.erase(old.generation);
    leases_.pop_back();
    return old;
  }
  std::optional<BufferSrvLease> Erase(uint64_t generation) {
    auto found=by_generation_.find(generation);
    if(found==by_generation_.end()) return std::nullopt;
    const auto old=*found->second;
    leases_.erase(found->second);
    by_generation_.erase(found);
    return old;
  }
  size_t size() const {return leases_.size();}
 private:
  size_t capacity_;
  std::list<BufferSrvLease> leases_;
  std::unordered_map<uint64_t,std::list<BufferSrvLease>::iterator> by_generation_;
};
} // namespace superman_returns::native
