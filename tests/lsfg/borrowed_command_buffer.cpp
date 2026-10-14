#include "lsfg-vk-common/vulkan/command_buffer.hpp"
#include <cstdio>
int main() {
  auto raw=reinterpret_cast<VkCommandBuffer>(uintptr_t(123));
  {vk::CommandBuffer view(raw);if(!view.borrowed() || view.handle()!=raw) return 1;}
  std::puts("borrowed command buffer ownership passed");
}
