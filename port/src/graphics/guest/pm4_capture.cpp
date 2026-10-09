#include "pm4_capture.h"
#include "../../native_renderer/pm4_mirror.h"
#include <chrono>
#include <deque>
#include <exception>
#include <memory>
#include <stdexcept>

namespace superman_returns::graphics::guest {
namespace {
inline uint64_t NowNs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}
} // namespace
bool CapturePm4Dependencies(WorkBatch &batch, const WorkCmd &cmd,
                            const GuestMemoryReader &read, std::string &error,
                            native::Pm4Mirror *mirror, Pm4CaptureProbe *probe) {
  error.clear();
  if (!cmd.ring_bytes)
    return true;
  if (uint64_t(cmd.ring_offset) + cmd.ring_bytes > batch.bytes.size()) {
    error = "Primary PM4 capture exceeds batch";
    return false;
  }
  // Appending dependencies may grow the arena. Keep primary and all recursive
  // indirect streams stable for the entire scan, including nested callbacks.
  const uint64_t primary_t0 = probe ? NowNs() : 0;
  std::vector<uint8_t> primary(batch.bytes.begin() + cmd.ring_offset,
                               batch.bytes.begin() + cmd.ring_offset +
                                   cmd.ring_bytes);
  if (probe)
    probe->primary_ns += NowNs() - primary_t0;
  std::deque<std::vector<uint8_t>> sources;
  const size_t initial_bytes = batch.bytes.size(),
               initial_ranges = batch.ranges.size();
  try {
    // Production capture supplies a persistent mirror. Keep the large fallback
    // off its stack and construct it only for callers that need a fresh parser.
    std::unique_ptr<native::Pm4Mirror> local;
    if (!mirror) local = std::make_unique<native::Pm4Mirror>();
    auto &scanner = mirror ? *mirror : *local;
    auto alu_before = scanner.unreadable_alu_loads;
    auto indirect_before = scanner.unreadable_indirect_buffers;
    uint32_t missing_address = 0, missing_length = 0;
    scanner.ScanCopyUsing(
        primary.data(), cmd.ring_bytes,
        [&](uint32_t address, uint32_t length) -> std::span<const uint8_t> {
          if (uint64_t(address) + length > (uint64_t{1} << 32))
            return {};
          const uint64_t read_t0 = probe ? NowNs() : 0;
          auto source = read(address, length);
          if (probe)
            probe->read_ns += NowNs() - read_t0;
          if (source.size() < length) {
            missing_address = address;
            missing_length = length;
            return {};
          }
          if (uint64_t(batch.bytes.size()) + length > UINT32_MAX)
            throw std::length_error(
                "PM4 dependency capture exceeds arena limit");
          if (probe) {
            ++probe->reads;
            probe->bytes += length;
          }
          const uint64_t copy_t0 = probe ? NowNs() : 0;
          sources.emplace_back(source.begin(), source.begin() + length);
          auto &owned = sources.back();
          auto offset = uint32_t(batch.bytes.size());
          batch.bytes.insert(batch.bytes.end(), owned.begin(), owned.end());
          batch.ranges.push_back({address, length, offset});
          if (probe)
            probe->copy_ns += NowNs() - copy_t0;
          return owned;
        });
    if (scanner.unreadable_alu_loads == alu_before &&
        scanner.unreadable_indirect_buffers == indirect_before)
      return true;
    error =
        "Referenced PM4 memory was not readable during capture: address=" +
        std::to_string(missing_address) +
        " length=" + std::to_string(missing_length) +
        " alu=" + std::to_string(scanner.unreadable_alu_loads - alu_before) +
        " indirect=" +
        std::to_string(scanner.unreadable_indirect_buffers - indirect_before);
  } catch (const std::exception &e) {
    error = e.what();
  }
  batch.bytes.resize(initial_bytes);
  batch.ranges.resize(initial_ranges);
  return false;
}
} // namespace superman_returns::graphics::guest
