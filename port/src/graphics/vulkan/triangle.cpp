#include "triangle.h"
#include <cstring>
#include <fstream>
namespace superman_returns::graphics::vulkan {
bool ValidSpirv(std::span<const uint32_t> w) {
  return w.size() >= 5 && w[0] == 0x07230203 && w[1] >= 0x00010000 &&
         w[1] <= 0x00010300 && w[3] > 0 && w[4] == 0;
}
bool ReadSpirv(const std::filesystem::path &path, std::vector<uint32_t> &w,
               Error &e) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  auto size = stream.tellg();
  if (!stream || size < 20 || size % 4 || size > 16777216) {
    e = {"ReadSpirv", VK_ERROR_INITIALIZATION_FAILED,
         "Shader missing or invalid: " + path.string()};
    return false;
  }
  w.resize(size_t(size) / 4);
  stream.seekg(0);
  if (!stream.read(reinterpret_cast<char *>(w.data()), size) ||
      !ValidSpirv(w)) {
    e = {"ReadSpirv", VK_ERROR_INITIALIZATION_FAILED,
         "Invalid SPIR-V header: " + path.string()};
    return false;
  }
  return true;
}
UploadBuffer::~UploadBuffer() { Destroy(); }
void UploadBuffer::Destroy() {
  if (!context_)
    return;
  auto &c = *context_;
  if (handle)
    c.f.vkDestroyBuffer(c.device, handle, nullptr);
  if (memory_)
    c.f.vkFreeMemory(c.device, memory_, nullptr);
  handle = VK_NULL_HANDLE;
  memory_ = VK_NULL_HANDLE;
  context_ = nullptr;
}
bool UploadBuffer::Initialize(Context &c, std::span<const std::byte> data,
                              Error &e) {
  if (context_ || data.empty()) {
    e = {"UploadBuffer", VK_ERROR_INITIALIZATION_FAILED,
         "Buffer already initialized or empty"};
    return false;
  }
  context_ = &c;
  VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  ci.size = data.size();
  ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (!Check(c.f.vkCreateBuffer(c.device, &ci, nullptr, &handle),
             "CreateBuffer", e))
    return false;
  VkMemoryRequirements req{};
  c.f.vkGetBufferMemoryRequirements(c.device, handle, &req);
  std::vector<VkMemoryPropertyFlags> flags;
  for (uint32_t i = 0; i < c.memory.memoryTypeCount; ++i)
    flags.push_back(c.memory.memoryTypes[i].propertyFlags);
  auto type = ChooseMemoryType(req.memoryTypeBits, flags,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (!type) {
    e = {"Upload memory", VK_ERROR_FEATURE_NOT_PRESENT,
         "No host-visible memory type"};
    return false;
  }
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = *type;
  if (!Check(c.f.vkAllocateMemory(c.device, &ai, nullptr, &memory_),
             "AllocateMemory", e) ||
      !Check(c.f.vkBindBufferMemory(c.device, handle, memory_, 0),
             "BindBufferMemory", e))
    return false;
  void *mapped = nullptr;
  if (!Check(c.f.vkMapMemory(c.device, memory_, 0, req.size, 0, &mapped),
             "MapMemory", e))
    return false;
  std::memcpy(mapped, data.data(), data.size());
  bool ok = true;
  if (!(flags[*type] & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
    auto range = AlignFlushRange(0, data.size(), req.size,
                                 c.properties.limits.nonCoherentAtomSize);
    if (!range.valid) {
      e = {"Upload flush", VK_ERROR_INITIALIZATION_FAILED,
           "Invalid memory flush range"};
      ok = false;
    } else {
      VkMappedMemoryRange mr{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
      mr.memory = memory_;
      mr.offset = range.offset;
      mr.size = range.size;
      ok = Check(c.f.vkFlushMappedMemoryRanges(c.device, 1, &mr),
                 "FlushMappedMemoryRanges", e);
    }
  }
  c.f.vkUnmapMemory(c.device, memory_);
  return ok;
}
Triangle::~Triangle() { Destroy(); }
void Triangle::Destroy() {
  if (!context_)
    return;
  auto &c = *context_;
  if (pipeline_)
    c.f.vkDestroyPipeline(c.device, pipeline_, nullptr);
  if (layout_)
    c.f.vkDestroyPipelineLayout(c.device, layout_, nullptr);
  pipeline_ = VK_NULL_HANDLE;
  layout_ = VK_NULL_HANDLE;
  vertices_.Destroy();
  context_ = nullptr;
}
bool Triangle::Initialize(Context &c, VkRenderPass pass,
                          std::span<const uint32_t> vs,
                          std::span<const uint32_t> ps, Error &e) {
  if (context_) {
    e = {"Triangle", VK_ERROR_INITIALIZATION_FAILED,
         "Triangle already initialized"};
    return false;
  }
  if (!ValidSpirv(vs) || !ValidSpirv(ps)) {
    e = {"Triangle shaders", VK_ERROR_INITIALIZATION_FAILED, "Invalid SPIR-V"};
    return false;
  }
  context_ = &c;
  struct Vertex {
    float x, y, r, g, b;
  };
  const Vertex vertices[] = {
      {-.6f, -.55f, 1, 0, 0}, {.6f, -.55f, 0, 1, 0}, {0, .55f, 0, 0, 1}};
  if (!vertices_.Initialize(c, std::as_bytes(std::span(vertices)), e))
    return false;
  VkShaderModule modules[2]{};
  auto free_modules = [&]() {
    for (auto h : modules)
      if (h)
        c.f.vkDestroyShaderModule(c.device, h, nullptr);
  };
  int i = 0;
  for (auto words : {vs, ps}) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = words.size_bytes();
    ci.pCode = words.data();
    if (!Check(c.f.vkCreateShaderModule(c.device, &ci, nullptr, &modules[i++]),
               "CreateShaderModule", e)) {
      free_modules();
      return false;
    }
  }
  VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  if (!Check(c.f.vkCreatePipelineLayout(c.device, &li, nullptr, &layout_),
             "CreatePipelineLayout", e)) {
    free_modules();
    return false;
  }
  VkPipelineShaderStageCreateInfo stages[2]{};
  for (auto &stage : stages)
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = modules[0];
  stages[0].pName = "VSMain";
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = modules[1];
  stages[1].pName = "PSMain";
  VkVertexInputBindingDescription binding{0, sizeof(Vertex),
                                          VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription attributes[] = {
      {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
      {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 8}};
  VkPipelineVertexInputStateCreateInfo input{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  input.vertexBindingDescriptionCount = 1;
  input.pVertexBindingDescriptions = &binding;
  input.vertexAttributeDescriptionCount = 2;
  input.pVertexAttributeDescriptions = attributes;
  VkPipelineInputAssemblyStateCreateInfo assembly{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo viewport{
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport.viewportCount = viewport.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo raster{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1;
  VkPipelineMultisampleStateCreateInfo msaa{
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState color{};
  color.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = 1;
  blend.pAttachments = &color;
  VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT,
                               VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn{
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dyn.dynamicStateCount = 2;
  dyn.pDynamicStates = dynamics;
  VkGraphicsPipelineCreateInfo ci{
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  ci.stageCount = 2;
  ci.pStages = stages;
  ci.pVertexInputState = &input;
  ci.pInputAssemblyState = &assembly;
  ci.pViewportState = &viewport;
  ci.pRasterizationState = &raster;
  ci.pMultisampleState = &msaa;
  ci.pColorBlendState = &blend;
  ci.pDynamicState = &dyn;
  ci.layout = layout_;
  ci.renderPass = pass;
  auto result = c.f.vkCreateGraphicsPipelines(c.device, VK_NULL_HANDLE, 1, &ci,
                                              nullptr, &pipeline_);
  free_modules();
  return Check(result, "CreateGraphicsPipelines", e);
}
void Triangle::Record(VkCommandBuffer command, VkExtent2D extent) {
  auto &f = context_->f;
  VkViewport v{0, 0, float(extent.width), float(extent.height), 0, 1};
  VkRect2D r{{0, 0}, extent};
  VkDeviceSize offset = 0;
  f.vkCmdSetViewport(command, 0, 1, &v);
  f.vkCmdSetScissor(command, 0, 1, &r);
  f.vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
  f.vkCmdBindVertexBuffers(command, 0, 1, &vertices_.handle, &offset);
  f.vkCmdDraw(command, 3, 1, 0, 0);
}
} // namespace superman_returns::graphics::vulkan
