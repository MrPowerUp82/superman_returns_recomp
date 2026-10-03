#pragma once
#include <cstdint>
#include <span>

namespace superman_returns::graphics::guest {
struct VertexAttribute {uint32_t stream,offset,type,usage,index;};
struct VertexEncoding {uint32_t size,swap;bool floating;};
VertexEncoding GetVertexEncoding(uint32_t xdk_type);
// Incomplete vertices and padding retain the original bytes. A stream of
// UINT32_MAX includes all attributes, matching the XDK inline draw path.
void SwapVertexElements(std::span<uint8_t>,uint32_t stride,uint32_t phase,
                        uint32_t stream,std::span<const VertexAttribute>);
}
