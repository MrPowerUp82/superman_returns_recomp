#include "texture_capture.h"
#include <algorithm>
#include <exception>

namespace superman_returns::graphics::guest {
bool CaptureTexture(std::span<const uint32_t, 6> fetch, uint64_t version,
                    const GuestMemoryReader &read,
                    std::shared_ptr<const TextureCapture> &out,
                    std::string &error) {
  try {
    auto capture = std::make_shared<TextureCapture>();
    if (!DescribeTextureRanges(fetch, capture->ranges, error))
      return false;
    WorkBatch batch;
    for (const auto &range : capture->ranges) {
      uint32_t address = 0xa0000000u + range.address;
      auto bytes = read(address, range.length);
      if (bytes.size() < range.length) {
        error = "Texture source unreadable during capture";
        return false;
      }
      if (uint64_t(batch.bytes.size()) + range.length > UINT32_MAX) {
        error = "Texture capture arena overflow";
        return false;
      }
      batch.ranges.push_back(
          {address, range.length, uint32_t(batch.bytes.size())});
      batch.bytes.insert(batch.bytes.end(), bytes.begin(),
                         bytes.begin() + range.length);
    }
    WorkCmd command;
    command.range_count = uint32_t(batch.ranges.size());
    if (!CapturedMemory::Capture(batch, command, capture->memory, error))
      return false;
    std::copy(fetch.begin(), fetch.end(), capture->fetch.begin());
    capture->version = version;
    out = std::move(capture);
    return true;
  } catch (const std::exception &e) {
    error = e.what();
    return false;
  }
}
} // namespace superman_returns::graphics::guest
