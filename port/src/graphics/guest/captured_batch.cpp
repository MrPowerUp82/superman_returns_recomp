#include "captured_batch.h"
#include <exception>
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
      if(partial || offset+length>bytes.size()) return {};
      return bytes.subspan(size_t(offset),length);
    }
    partial=true;
  }
  return {};
}
std::span<const uint8_t> CapturedMemory::ReadUnambiguous(uint32_t address,uint32_t length) const {
  return snapshot_?FindUnambiguousCapture(snapshot_->ranges,snapshot_->bytes,address,length):std::span<const uint8_t>{};
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
    return true;
  } catch (const std::exception &e) {
    error = e.what();
    return false;
  }
}
std::span<const uint8_t> CapturedMemory::Read(uint32_t address,
                                              uint32_t length) const {
  if (!length)
    return {};
  if (snapshot_ && uint64_t(address) + length <= (uint64_t(1) << 32)) {
    for (auto i = snapshot_->ranges.rbegin(); i != snapshot_->ranges.rend();
         ++i) {
      if (address < i->address)
        continue;
      auto offset = uint64_t(address) - i->address;
      if (offset <= i->length && length <= uint64_t(i->length) - offset)
        return std::span(snapshot_->bytes)
            .subspan(size_t(i->offset + offset), length);
    }
  }
  throw std::out_of_range("Guest bytes absent from captured command");
}
} // namespace superman_returns::graphics::guest
