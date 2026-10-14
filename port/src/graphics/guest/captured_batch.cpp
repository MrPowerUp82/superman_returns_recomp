#include "captured_batch.h"
#include <exception>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace superman_returns::graphics::guest {
std::span<const uint8_t> FindUnambiguousCapture(std::span<const CaptureRange> ranges,
    std::span<const uint8_t> bytes,uint32_t address,uint32_t length) {
  const uint64_t end=uint64_t(address)+length;
  if(!length || end>(uint64_t(1)<<32)) return {};
  bool partial=false;
  for(auto i=ranges.rbegin();i!=ranges.rend();++i) {
    const uint64_t range_end=uint64_t(i->address)+i->length;
    if(address>=range_end || end<=i->address) continue;
    if(address>=i->address && end<=range_end) {
      const uint64_t offset=uint64_t(i->offset)+address-i->address;
      if(offset+length>bytes.size()) return {};
      if(partial) {
        // A later suffix capture may duplicate an earlier complete bank.
        // Borrow the bank only after proving every newer intersection equal;
        // any changed byte keeps the original scalar overlay behavior.
        for(auto newer=ranges.rbegin();newer!=i;++newer) {
          const uint64_t newer_end=uint64_t(newer->address)+newer->length;
          if(address>=newer_end || end<=newer->address) continue;
          const uint64_t overlap_begin=address>newer->address?address:newer->address;
          const uint64_t overlap_end=end<newer_end?end:newer_end;
          const uint64_t newer_offset=uint64_t(newer->offset)+overlap_begin-newer->address;
          const uint64_t size=overlap_end-overlap_begin;
          if(newer_offset+size>bytes.size() ||
             std::memcmp(bytes.data()+size_t(offset+overlap_begin-address),
                         bytes.data()+size_t(newer_offset),size_t(size))!=0) return {};
        }
      }
      return bytes.subspan(size_t(offset),length);
    }
    partial=true;
  }
  return {};
}
std::span<const uint8_t> CapturedMemory::ReadUnambiguous(uint32_t address,uint32_t length) const {
  return FindUnambiguousCapture(Ranges(),Bytes(),address,length);
}
std::span<const CaptureRange> CapturedMemory::Ranges() const {
  if(batch_) return std::span<const CaptureRange>(batch_->ranges).subspan(range_first_,range_count_);
  return snapshot_?std::span<const CaptureRange>(snapshot_->ranges):std::span<const CaptureRange>{};
}
std::span<const uint8_t> CapturedMemory::Bytes() const {
  if(batch_) return {batch_->bytes.data(),batch_->bytes.size()};
  return snapshot_?std::span<const uint8_t>(snapshot_->bytes):std::span<const uint8_t>{};
}
bool CapturedMemory::Capture(const WorkBatch &batch, const WorkCmd &cmd,
                             CapturedMemory &out, std::string &error) {
  error.clear();
  if (uint64_t(cmd.range_first) + cmd.range_count > batch.ranges.size()) {
    error = "Command capture range exceeds batch";
    return false;
  }
  try {
    uint64_t total_bytes = 0;
    for (uint64_t i = cmd.range_first;
         i < uint64_t(cmd.range_first) + cmd.range_count; ++i) {
      const auto &range = batch.ranges[size_t(i)];
      if (uint64_t(range.address) + range.length > (uint64_t(1) << 32) ||
          uint64_t(range.offset) + range.length > batch.bytes.size() ||
          total_bytes + range.length > UINT32_MAX) {
        error = "Captured memory range exceeds address or byte bounds";
        return false;
      }
      total_bytes += range.length;
    }
    auto snapshot = std::make_shared<Snapshot>();
    // Allocate once instead of repeatedly reallocating and copying all prior
    // ranges. Keep capture order so overlapping reads still prefer the latest.
    snapshot->bytes.reserve(size_t(total_bytes));
    snapshot->ranges.reserve(cmd.range_count);
    for (uint64_t i = cmd.range_first;
         i < uint64_t(cmd.range_first) + cmd.range_count; ++i) {
      const auto &range = batch.ranges[size_t(i)];
      auto offset = uint32_t(snapshot->bytes.size());
      snapshot->bytes.insert(
          snapshot->bytes.end(), batch.bytes.begin() + range.offset,
          batch.bytes.begin() + size_t(range.offset) + range.length);
      snapshot->ranges.push_back({range.address, range.length, offset});
    }
    out.snapshot_ = std::move(snapshot);
    out.batch_.reset();
    return true;
  } catch (const std::exception &e) {
    error = e.what();
    return false;
  }
}
bool CapturedMemory::CaptureOwned(std::shared_ptr<const WorkBatch> batch,const WorkCmd& cmd,
                                 CapturedMemory& out,std::string& error) {
  if(!batch) {error="Missing owned capture batch";return false;}
  error.clear();
  if(uint64_t(cmd.range_first)+cmd.range_count>batch->ranges.size()) {
    error="Command capture range exceeds batch";return false;
  }
  uint64_t total=0;
  for(uint64_t i=cmd.range_first;i<uint64_t(cmd.range_first)+cmd.range_count;++i) {
    const auto& range=batch->ranges[size_t(i)];
    if(uint64_t(range.address)+range.length>(uint64_t{1}<<32) ||
       uint64_t(range.offset)+range.length>batch->bytes.size() || total+range.length>UINT32_MAX) {
      error="Captured memory range exceeds address or byte bounds";return false;
    }
    total+=range.length;
  }
  out.snapshot_.reset();out.batch_=std::move(batch);
  out.range_first_=cmd.range_first;out.range_count_=cmd.range_count;
  return true;
}
std::span<const uint8_t> CapturedMemory::Read(uint32_t address,
                                              uint32_t length) const {
  if (!length)
    return {};
  const auto ranges=Ranges();
  if (uint64_t(address) + length <= (uint64_t(1) << 32)) {
    for (auto i = ranges.rbegin(); i != ranges.rend();
         ++i) {
      if (address < i->address)
        continue;
      auto offset = uint64_t(address) - i->address;
      if (offset <= i->length && length <= uint64_t(i->length) - offset)
        return Bytes()
            .subspan(size_t(i->offset + offset), length);
    }
  }
  throw std::out_of_range("Guest bytes absent from captured command");
}
} // namespace superman_returns::graphics::guest
