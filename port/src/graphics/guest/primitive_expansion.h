#pragma once
#include "vertex_layout.h"
#include <string>
#include <vector>
#include <cstddef>
namespace superman_returns::graphics::guest {
bool ExpandRectangles(std::span<const std::byte>,uint32_t count,uint32_t stride,
    std::span<const VertexAttribute>,std::vector<uint8_t>&,std::string&);
}
