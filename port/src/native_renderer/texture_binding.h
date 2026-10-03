#pragma once

#include <cstdint>

namespace superman_returns::native {

// PM4 fetch constants are the binding state. Raw fetch bindings (movies and
// UI) need no D3DTexture object in the XDK device shadow.
constexpr bool IsTextureBound(uint32_t fetch_type) {
  return (fetch_type & 3u) == 2u;
}

}  // namespace superman_returns::native
