#pragma once
#include "observed_instance_lifetimes.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <span>
#include <unordered_map>
#include <vector>

namespace superman_returns::graphics {
inline uint64_t VehicleBatchHashWord(uint64_t hash,uint64_t word) {
  for(unsigned byte=0;byte<8;++byte) {hash^=(word>>(byte*8))&255;hash*=1099511628211ull;}
  return hash;
}
inline uint64_t VehicleBatchHashWords(std::span<const uint32_t> words) {
  uint64_t hash=VehicleBatchHashWord(14695981039346656037ull,0x56425031);
  for(uint32_t word:words) hash=VehicleBatchHashWord(hash,word);
  return hash;
}
struct VehicleBatchShape {
  uint32_t buffer=0,packet=0,rows=0,groups=0;
  bool operator==(const VehicleBatchShape&) const=default;
};
struct VehicleBatchMember {
  ObservedInstanceToken token;
  std::array<uint64_t,9> part_words{};
  std::array<uint32_t,24> palette{};
};
inline bool ValidVehicleBatchMember(const VehicleBatchMember& member) {
  const auto& p=member.part_words;
  if(member.token.address<0x10000 || (member.token.address&3) || !member.token.generation ||
     p[0]!=member.token.address || p[1]!=0x823103C8 || p[2]!=0x82021930 ||
     p[3]<0x10000 || (p[3]&3) || p[4]<0x10000 || (p[4]&3) || p[5]!=9 ||
     p[6]>0xffff || p[7]!=p[6]*4 || p[8]!=p[6]) return false;
  for(uint32_t word:member.palette) if(!std::isfinite(std::bit_cast<float>(word))) return false;
  return !(member.palette[3]&0x7fffffff) && !(member.palette[7]&0x7fffffff) &&
      !(member.palette[11]&0x7fffffff) && member.palette[15]==std::bit_cast<uint32_t>(1.f);
}
// Contains observations of exact copied queue slots, not inferred matrix matches.
// Callers synchronize access and provide actual before/after memory snapshots.
class ObservedVehicleBatchRegistry {
 public:
  static constexpr uint32_t kMaxGroups=1024,kMaxMembers=100;
  bool Contains(uint32_t entry) const {return groups_.contains(uint64_t(entry)*2) || groups_.contains(uint64_t(entry)*2+1);}
  void Clear(uint32_t entry) {groups_.erase(uint64_t(entry)*2);groups_.erase(uint64_t(entry)*2+1);}
  bool ObserveAppend(uint32_t entry,uint32_t group,VehicleBatchShape shape,uint32_t before,uint32_t after,
      const VehicleBatchMember* member,std::span<const uint32_t,24> copied) {
    const uint64_t key=uint64_t(entry)*2+group;
    if(entry<0x10000 || (entry&3) || group>1 || shape.groups<1 || shape.groups>2 || group>=shape.groups ||
       shape.rows!=6 || shape.buffer<0x10000 || (shape.buffer&15) || shape.packet<0x10000 || (shape.packet&3) ||
       before>kMaxMembers || after>kMaxMembers) {Clear(entry);return false;}
    auto found=groups_.find(key);
    if(after==before) {
      if(found!=groups_.end() && (found->second.shape!=shape || found->second.members.size()!=before)) groups_.erase(found);
      return false; // Full queues do not append; do not read an uninitialized destination.
    }
    if(after!=before+1) {groups_.erase(key);return false;}
    if(found==groups_.end()) {
      if(groups_.size()>=kMaxGroups) return false;
      found=groups_.try_emplace(key).first;
    }
    auto& record=found->second;
    if(!before || record.shape!=shape || record.members.size()!=before) {
      record.shape=shape;record.members.assign(before,{}); // Unknown prefix is never qualified.
    }
    Slot slot;
    if(member && ValidVehicleBatchMember(*member) && std::equal(copied.begin(),copied.end(),member->palette.begin())) {
      slot.member=*member;slot.known=true;
    }
    record.members.push_back(slot);return slot.known;
  }
  bool CaptureUpload(uint32_t entry,std::span<const VehicleBatchShape,2> shapes,
      std::span<const uint32_t,2> counts,uint32_t source,uint32_t rows,std::span<const uint32_t> words,
      const ObservedInstanceLifetimes& lifetimes,ObservedVehicleBatchSelection& output) const {
    if(!source || !rows || rows%6 || rows>126 || words.size()!=size_t(rows)*4) return false;
    const uint32_t members=rows/6;const Record* selected=nullptr;uint32_t first=0;
    for(uint32_t group=0;group<2;++group) {
      if(!counts[group]) continue;
      const auto found=groups_.find(uint64_t(entry)*2+group);
      if(found==groups_.end() || found->second.shape!=shapes[group] ||
         found->second.members.size()!=counts[group] || shapes[group].groups<=group || source<shapes[group].buffer) continue;
      const uint64_t offset=uint64_t(source)-shapes[group].buffer;
      if(offset%96 || offset/96+members>counts[group]) continue;
      if(selected) return false; // Aliased group pointers are ambiguous, not a nearest match.
      selected=&found->second;first=uint32_t(offset/96);
    }
    if(!selected) return false;
    uint64_t identity=VehicleBatchHashWord(14695981039346656037ull,0x56424931);
    uint64_t generation=VehicleBatchHashWord(14695981039346656037ull,0x56424731);
    identity=VehicleBatchHashWord(identity,members);generation=VehicleBatchHashWord(generation,members);
    for(uint32_t i=0;i<members;++i) {
      const auto& slot=selected->members[first+i];
      if(!slot.known || !lifetimes.IsAlive(slot.member.token) ||
         !std::equal(slot.member.palette.begin(),slot.member.palette.end(),words.begin()+i*24)) return false;
      for(uint32_t j=0;j<i;++j) {
        const auto& earlier=selected->members[first+j].member;
        if(earlier.token==slot.member.token && earlier.part_words==slot.member.part_words &&
           earlier.palette!=slot.member.palette) return false;
      }
      for(uint64_t word:slot.member.part_words) identity=VehicleBatchHashWord(identity,word);
      generation=VehicleBatchHashWord(generation,slot.member.token.address);
      generation=VehicleBatchHashWord(generation,slot.member.token.generation);
    }
    const auto palette=VehicleBatchHashWords(words);
    if(!identity || !generation || !palette) return false;
    output={identity,generation,palette,members,6,true};return true;
  }
 private:
  struct Slot {VehicleBatchMember member;bool known=false;};
  struct Record {VehicleBatchShape shape;std::vector<Slot> members;};
  std::unordered_map<uint64_t,Record> groups_;
};
struct ObservedVehicleBatchShaderInputs {
  uint64_t instance_key=0,generation_key=0;
  std::array<uint32_t,1028> position_words{};
  std::array<uint32_t,16> clip_rows{};
};
inline bool CaptureObservedVehicleBatchShaderInputs(uint64_t shader,const ObservedVehicleBatchSelection& batch,
    std::span<const uint32_t,1024> floats,std::span<const uint32_t,4> bools,
    bool shader_fetch,bool indexed,uint32_t first,uint32_t end,ObservedVehicleBatchShaderInputs& output) {
  if((shader!=0x9A62E7C65051447Aull && shader!=0x315E6843ECE68A00ull) ||
     !batch.source_route_observed || !batch.instance_key || !batch.generation_key || !batch.palette_signature ||
     batch.rows!=6 || !batch.members || batch.members>21 || !shader_fetch || indexed || first || !end || end>0x1000000) return false;
  const float stride=std::bit_cast<float>(floats[16]);
  if(!std::isfinite(stride) || stride<1 || stride>65536 || std::floor(stride)!=stride ||
     uint64_t(uint32_t(stride))*batch.members!=end) return false;
  for(size_t i=17;i<20;++i) if(floats[i]&0x7fffffff) return false;
  const float infinity=std::numeric_limits<float>::infinity();
  const auto lower=[&](float v){return std::nextafter(v,-infinity);};
  const auto upper=[&](float v){return std::nextafter(v,infinity);};
  const float reciprocal=1.f/stride;
  const float low=lower(lower(.5f)*lower(lower(reciprocal)));
  const float high=upper(upper(float(end-1)+.5f)*upper(upper(reciprocal)));
  if(low<0 || !(high<float(batch.members)) ||
     VehicleBatchHashWords(floats.subspan(400,batch.members*24))!=batch.palette_signature) return false;
  ObservedVehicleBatchShaderInputs result;
  for(size_t i=0;i<floats.size();++i) {
    if(!std::isfinite(std::bit_cast<float>(floats[i]))) return false;
    result.position_words[i]=floats[i];
  }
  std::copy(bools.begin(),bools.end(),result.position_words.begin()+1024);
  std::copy_n(floats.begin()+232*4,16,result.clip_rows.begin());
  result.instance_key=batch.instance_key;result.generation_key=batch.generation_key;output=result;return true;
}
}
