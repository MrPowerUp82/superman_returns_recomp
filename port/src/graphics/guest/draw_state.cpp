#include "draw_state.h"
#include <exception>
namespace superman_returns::graphics::guest {
bool NormalizeIndices(std::span<const uint8_t> source, uint32_t first,
                      uint32_t count, IndexEncoding encoding,
                      Primitive primitive, std::vector<uint32_t> &out,
                      std::string &error) {
  error.clear();
  const uint32_t width = encoding.index32 ? 4 : 2;
  const uint64_t end = uint64_t(first) + count;
  if (end * width > source.size()) {
    error = "Index range exceeds captured bytes";
    return false;
  }
  if (primitive == Primitive::kQuads && count % 4) {
    error = "Incomplete quad";
    return false;
  }
  const bool strip = primitive == Primitive::kTriangleStrip ||
                     primitive == Primitive::kLineStrip;
  const uint32_t reset = strip ? encoding.restart : UINT32_MAX;
  try {
    std::vector<uint32_t> values;
    values.reserve(count);
    for (uint64_t i = first; i < end; ++i) {
      uint32_t value = 0;
      for (uint32_t b = 0; b < width; ++b) {
        uint64_t address = (i * width + b) ^ IndexByteXor(encoding.endian);
        if (address >= source.size()) {
          error = "Endian index load exceeds captured bytes";
          return false;
        }
        value |= uint32_t(source[size_t(address)]) << (b * 8);
      }
      values.push_back(HostStripIndex(value, encoding.index32, reset));
    }
    if (primitive == Primitive::kQuads) {
      std::vector<uint32_t> triangles;
      triangles.reserve(size_t(count / 4) * 6);
      for (size_t i = 0; i < values.size(); i += 4)
        triangles.insert(triangles.end(),
                         {values[i], values[i + 1], values[i + 2], values[i],
                          values[i + 2], values[i + 3]});
      values.swap(triangles);
    }
    out.swap(values);
    return true;
  } catch (const std::exception &e) {
    error = e.what();
    return false;
  }
}
} // namespace superman_returns::graphics::guest
