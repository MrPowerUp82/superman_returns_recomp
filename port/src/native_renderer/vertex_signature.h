#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
namespace superman_returns::native {
inline std::vector<std::pair<std::string, uint32_t>> VertexShaderInputs(const std::vector<uint8_t>& dxbc, bool float_only = false) {
  std::vector<std::pair<std::string, uint32_t>> out;
  auto rd = [&](size_t at) -> uint32_t {
    uint32_t v = 0;
    if (at + 4 <= dxbc.size()) std::memcpy(&v, dxbc.data() + at, 4);
    return v;
  };
  if (dxbc.size() < 32 || std::memcmp(dxbc.data(), "DXBC", 4) != 0) return out;
  const uint32_t chunks = rd(28);
  for (uint32_t c = 0; c < chunks && c < 64; ++c) {
    const size_t at = rd(32 + 4 * c);
    if (at + 8 > dxbc.size() || std::memcmp(dxbc.data() + at, "ISG1", 4) != 0) continue;
    const size_t base = at + 8;
    const size_t end = std::min(dxbc.size(), base + rd(at + 4));
    const uint32_t count = rd(base);
    for (uint32_t i = 0; i < count && i < 64; ++i) {
      const size_t e = base + rd(base + 4) + 32 * i;
      if (e + 32 > end) break;
      // Generated system inputs have no IA element. Only float inputs can
      // use the XDK's (0, 0, 0, 1) fallback without packed-format decoding.
      if (rd(e + 12) != 0 || (float_only && rd(e + 16) != 3)) continue;
      const size_t name_at = base + rd(e + 4);
      if (name_at >= end) continue;
      const char* name = reinterpret_cast<const char*>(dxbc.data() + name_at);
      out.emplace_back(std::string(name, strnlen(name, end - name_at)), rd(e + 8));
    }
    break;
  }
  return out;
}

}
