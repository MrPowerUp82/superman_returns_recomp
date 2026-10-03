#include "test_main.h"
#include "triangle.h"
using namespace superman_returns::graphics::vulkan;
namespace {
int flushes = 0;
std::vector<std::string> events;
std::byte storage[64];
VkResult VKAPI_CALL Buffer(VkDevice, const VkBufferCreateInfo *,
                           const VkAllocationCallbacks *, VkBuffer *b) {
  *b = reinterpret_cast<VkBuffer>(1);
  return VK_SUCCESS;
}
void VKAPI_CALL Requirements(VkDevice, VkBuffer, VkMemoryRequirements *r) {
  *r = {64, 4, 1};
}
VkResult VKAPI_CALL Allocate(VkDevice, const VkMemoryAllocateInfo *,
                             const VkAllocationCallbacks *, VkDeviceMemory *m) {
  *m = reinterpret_cast<VkDeviceMemory>(2);
  return VK_SUCCESS;
}
VkResult VKAPI_CALL Bind(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize) {
  return VK_SUCCESS;
}
VkResult VKAPI_CALL Map(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize,
                        VkMemoryMapFlags, void **p) {
  *p = storage;
  return VK_SUCCESS;
}
void VKAPI_CALL Unmap(VkDevice, VkDeviceMemory) {}
VkResult VKAPI_CALL Flush(VkDevice, uint32_t n, const VkMappedMemoryRange *r) {
  SR_CHECK_EQ(n, 1);
  SR_CHECK_EQ(r->offset, 0);
  SR_CHECK_EQ(r->size, 8);
  ++flushes;
  return VK_SUCCESS;
}
void VKAPI_CALL FreeBuffer(VkDevice, VkBuffer, const VkAllocationCallbacks *) {
  events.push_back("buffer");
}
void VKAPI_CALL FreeMemory(VkDevice, VkDeviceMemory,
                           const VkAllocationCallbacks *) {
  events.push_back("memory");
}
VkResult VKAPI_CALL Idle(VkDevice) { return VK_SUCCESS; }
VkResult VKAPI_CALL Shader(VkDevice, const VkShaderModuleCreateInfo *,
                           const VkAllocationCallbacks *, VkShaderModule *m) {
  *m = reinterpret_cast<VkShaderModule>(3);
  return VK_SUCCESS;
}
void VKAPI_CALL FreeShader(VkDevice, VkShaderModule,
                           const VkAllocationCallbacks *) {
  events.push_back("shader");
}
VkResult VKAPI_CALL Layout(VkDevice, const VkPipelineLayoutCreateInfo *,
                           const VkAllocationCallbacks *, VkPipelineLayout *l) {
  *l = reinterpret_cast<VkPipelineLayout>(4);
  return VK_SUCCESS;
}
void VKAPI_CALL FreeLayout(VkDevice, VkPipelineLayout,
                           const VkAllocationCallbacks *) {
  events.push_back("layout");
}
VkResult VKAPI_CALL Pipeline(VkDevice, VkPipelineCache, uint32_t,
                             const VkGraphicsPipelineCreateInfo *,
                             const VkAllocationCallbacks *, VkPipeline *) {
  return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}
Dispatch Fake() {
  Dispatch f;
  f.vkCreateBuffer = Buffer;
  f.vkGetBufferMemoryRequirements = Requirements;
  f.vkAllocateMemory = Allocate;
  f.vkBindBufferMemory = Bind;
  f.vkMapMemory = Map;
  f.vkUnmapMemory = Unmap;
  f.vkFlushMappedMemoryRanges = Flush;
  f.vkDestroyBuffer = FreeBuffer;
  f.vkFreeMemory = FreeMemory;
  f.vkDeviceWaitIdle = Idle;
  f.vkCreateShaderModule = Shader;
  f.vkDestroyShaderModule = FreeShader;
  f.vkCreatePipelineLayout = Layout;
  f.vkDestroyPipelineLayout = FreeLayout;
  f.vkCreateGraphicsPipelines = Pipeline;
  return f;
}
} // namespace
SR_TEST(truncated_or_invalid_spirv_is_rejected) {
  uint32_t good[] = {0x07230203, 0x00010300, 0, 2, 0};
  SR_CHECK(ValidSpirv(good));
  SR_CHECK(!ValidSpirv({good, 4}));
  good[0] = 0;
  SR_CHECK(!ValidSpirv(good));
}
SR_TEST(noncoherent_upload_flushes_before_submit) {
  Context c(Fake());
  c.device = reinterpret_cast<VkDevice>(1);
  c.memory.memoryTypeCount = 1;
  c.memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  c.properties.limits.nonCoherentAtomSize = 4;
  UploadBuffer b;
  Error e;
  flushes = 0;
  std::byte data[5]{};
  SR_CHECK(b.Initialize(c, data, e));
  SR_CHECK_EQ(flushes, 1);
}
SR_TEST(pipeline_failure_cleans_shader_modules_and_buffer) {
  events.clear();
  {
    Context c(Fake());
    c.device = reinterpret_cast<VkDevice>(1);
    c.memory.memoryTypeCount = 1;
    c.memory.memoryTypes[0].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    Triangle t;
    Error e;
    uint32_t good[] = {0x07230203, 0x00010300, 0, 2, 0};
    SR_CHECK(!t.Initialize(c, VK_NULL_HANDLE, good, good, e));
  }
  SR_CHECK(events == std::vector<std::string>(
                         {"shader", "shader", "layout", "buffer", "memory"}));
}
