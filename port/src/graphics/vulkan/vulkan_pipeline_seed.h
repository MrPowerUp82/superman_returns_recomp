#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <set>
#include <string>
#include <vector>

namespace superman_returns::graphics::vulkan {
// SRVPSO v1: numeric normalized GamePipelineKey words, never GPU handles or
// shader/container bytes. ABI scoped, checksummed, bounded, all-or-nothing.
inline uint64_t VulkanSeedChecksum(std::span<const uint8_t> bytes) {
  uint64_t hash=14695981039346656037ull;
  for(auto byte:bytes)hash=(hash^byte)*1099511628211ull;
  return hash;
}
inline std::vector<uint8_t> SerializeVulkanPipelineSeed(uint32_t abi,
    const std::vector<std::vector<uint64_t>>& records) {
  if((abi!=1 && abi!=2) || records.size()>8192)return {};
  std::vector<uint8_t> body;
  auto put=[&](std::vector<uint8_t>& out,uint64_t word,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i)out.push_back(uint8_t(word>>(8*i)));
  };
  for(const auto& record:records) {
    if(record.size()<19 || record.size()>2048)return {};
    put(body,record.size(),4);put(body,0,4);
    for(auto word:record)put(body,word,8);
    if(body.size()>64*1024*1024-32)return {};
  }
  std::vector<uint8_t> out{'S','R','V','P','S','O',0,0};
  put(out,1,4);put(out,abi,4);put(out,records.size(),4);put(out,0,4);
  put(out,VulkanSeedChecksum(body),8);out.insert(out.end(),body.begin(),body.end());return out;
}
inline bool ParseVulkanPipelineSeed(std::span<const uint8_t> bytes,uint32_t abi,
    std::vector<std::vector<uint64_t>>& records,std::string& error) {
  records.clear();
  auto fail=[&](const char* reason){error=reason;return false;};
  if(bytes.size()<32 || bytes.size()>64*1024*1024 || std::memcmp(bytes.data(),"SRVPSO\0\0",8))
    return fail("Invalid Vulkan description seed magic/size");
  auto get=[&](size_t at,unsigned count) {uint64_t word=0;for(unsigned i=0;i<count;++i)word|=uint64_t(bytes[at+i])<<(8*i);return word;};
  if(get(8,4)!=1 || get(12,4)!=abi || (abi!=1 && abi!=2) || get(20,4))return fail("Vulkan seed version/ABI mismatch");
  const auto count=get(16,4);
  if(count>8192 || get(24,8)!=VulkanSeedChecksum(bytes.subspan(32)))return fail("Vulkan seed count/checksum invalid");
  std::vector<std::vector<uint64_t>> parsed;size_t position=32;
  for(uint64_t i=0;i<count;++i) {
    if(bytes.size()-position<8)return fail("Truncated Vulkan seed entry");
    const auto words=get(position,4),reserved=get(position+4,4);position+=8;
    if(words<19 || words>2048 || reserved || words>(bytes.size()-position)/8)return fail("Invalid Vulkan seed entry bounds");
    std::vector<uint64_t> key;key.reserve(size_t(words));
    for(uint64_t j=0;j<words;++j,position+=8)key.push_back(get(position,8));
    parsed.push_back(std::move(key));
  }
  if(position!=bytes.size())return fail("Trailing Vulkan seed bytes");
  records=std::move(parsed);error.clear();return true;
}
// Merge only after complete validation. Never replace local observations or
// truncate them to make room for an embedded seed from another installation.
inline bool MergeVulkanPipelineSeed(std::span<const uint8_t> bytes,uint32_t abi,
    std::vector<std::vector<uint64_t>>& records,std::string& error) {
  std::vector<std::vector<uint64_t>> incoming;
  if(!ParseVulkanPipelineSeed(bytes,abi,incoming,error))return false;
  std::set<std::vector<uint64_t>> unique;
  std::vector<std::vector<uint64_t>> merged;
  auto append=[&](const auto& source) {
    for(const auto& key:source)if(unique.insert(key).second) {
      if(merged.size()==8192) {error="Merged Vulkan seed exceeds description capacity";return false;}
      merged.push_back(key);
    }
    return true;
  };
  if(!append(records) || !append(incoming))return false;
  records=std::move(merged);error.clear();return true;
}
}
