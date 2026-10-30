#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace superman_returns::native {

inline bool D3DTextureCanOverwrite(uint64_t last_use, uint64_t completed, uint64_t recording) {
  return last_use && completed!=UINT64_MAX && last_use<=completed && last_use<recording;
}

struct D3DTextureSnapshotBudget {
  size_t used=0;
  size_t limit=64*1024*1024;
};

// Worker-owned exact decoded bytes. Guest write proofs are deliberately separate:
// this snapshot describes the uploaded GPU version, not current guest memory.
class D3DTextureSnapshot {
  struct Level {
    uint32_t width,height,row_pitch,rows;
    size_t offset;
  };
  std::shared_ptr<D3DTextureSnapshotBudget> budget_;
  size_t charge_=0;
  std::vector<uint8_t> bytes_;
  std::vector<Level> levels_;

  template<class L> static bool Valid(std::span<const uint8_t> bytes,const L& level) {
    return level.width && level.height && level.row_pitch && level.rows &&
        level.offset<=bytes.size() && uint64_t(level.row_pitch)*level.rows<=bytes.size()-level.offset;
  }
public:
  ~D3DTextureSnapshot() { if(budget_) budget_->used-=charge_; }
  template<class B,size_t N,class L,size_t M>
  static std::unique_ptr<D3DTextureSnapshot> Capture(
      std::shared_ptr<D3DTextureSnapshotBudget> budget,std::span<B,N> bytes,std::span<L,M> levels) {
    constexpr size_t kPerTextureLimit=8*1024*1024;
    if(bytes.empty() || levels.empty() || bytes.size()>kPerTextureLimit ||
        levels.size()>(kPerTextureLimit-bytes.size())/sizeof(Level)) return {};
    const size_t charge=sizeof(D3DTextureSnapshot)+bytes.size()+levels.size()*sizeof(Level);
    if(charge>kPerTextureLimit || budget->used>budget->limit || charge>budget->limit-budget->used) return {};
    for(const auto& level:levels) if(!Valid(std::span<const uint8_t>(bytes),level)) return {};
    auto result=std::make_unique<D3DTextureSnapshot>();
    result->bytes_.assign(bytes.begin(),bytes.end());
    result->levels_.reserve(levels.size());
    for(const auto& l:levels) result->levels_.push_back({l.width,l.height,l.row_pitch,l.rows,l.offset});
    result->budget_=std::move(budget);result->charge_=charge;result->budget_->used+=charge;
    return result;
  }
  template<class B,size_t N,class L,size_t M>
  bool Matches(std::span<B,N> bytes,std::span<L,M> levels,size_t index) const {
    if(levels.size()!=levels_.size() || index>=levels.size()) return false;
    const auto& a=levels_[index];const auto& b=levels[index];
    if(a.width!=b.width || a.height!=b.height || a.row_pitch!=b.row_pitch || a.rows!=b.rows ||
        !Valid(std::span<const uint8_t>(bytes),b)) return false;
    return std::memcmp(bytes_.data()+a.offset,bytes.data()+b.offset,size_t(a.row_pitch)*a.rows)==0;
  }
};

} // namespace superman_returns::native
