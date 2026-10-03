#include "captured_batch.h"
#include <exception>
#include <limits>
#include <stdexcept>
namespace superman_returns::graphics::guest {
bool CapturedMemory::Capture(const WorkBatch &batch, const WorkCmd &cmd,
                             CapturedMemory &out, std::string &error) {
  error.clear();
  if (uint64_t(cmd.range_first) + cmd.range_count > batch.ranges.size()) {
    error = "Command capture range exceeds batch";
    return false;
  }
  try {
    auto snapshot = std::make_shared<Snapshot>();
    for (uint64_t i = cmd.range_first;
         i < uint64_t(cmd.range_first) + cmd.range_count; ++i) {
      const auto &range = batch.ranges[size_t(i)];
      if (uint64_t(range.address) + range.length > (uint64_t(1) << 32) ||
          uint64_t(range.offset) + range.length > batch.bytes.size() ||
          uint64_t(snapshot->bytes.size()) + range.length > UINT32_MAX) {
        error = "Captured memory range exceeds address or byte bounds";
        return false;
      }
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
