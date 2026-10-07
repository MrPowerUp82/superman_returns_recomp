#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <unordered_map>

namespace superman_returns::graphics::guest {
struct VertexAttribute {uint32_t stream,offset,type,usage,index;};
struct VertexEncoding {uint32_t size,swap;bool floating;};
struct VertexDeclaration {std::vector<VertexAttribute> attributes;uint32_t streams_used=0;};
// Per-thread, bounded cache keyed by owned bytes, never by a guest pointer.
// A returned reference is consumed before another Decode call can evict it.
class VertexDeclarationCache {
public:
  const VertexDeclaration& Decode(std::span<const uint8_t>);
private:
  struct Entry {std::vector<uint8_t> bytes;VertexDeclaration declaration;};
  std::unordered_map<size_t,std::vector<Entry>> entries_;
  const Entry* last_=nullptr;
  size_t count_=0;
};
VertexEncoding GetVertexEncoding(uint32_t xdk_type);
// Incomplete vertices and padding retain the original bytes. A stream of
// UINT32_MAX includes all attributes, matching the XDK inline draw path.
void SwapVertexElements(std::span<uint8_t>,uint32_t stride,uint32_t phase,
                        uint32_t stream,std::span<const VertexAttribute>);
}
