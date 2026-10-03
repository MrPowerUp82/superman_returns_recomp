// Project-owned GPU fixtures: upload bytes through the production store and
// read them back after a real fence. This executable does not render the game.
#include "resources.h"
#include "render_targets.h"
#include "platform/win32_surface.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace superman_returns::graphics::vulkan;
namespace guest=superman_returns::graphics::guest;
void Require(bool ok,const Error& e) {if(!ok) throw std::runtime_error(e.operation+": "+e.message);}
void Require(VkResult r,const char* operation) {if(r!=VK_SUCCESS) throw std::runtime_error(std::string(operation)+" result="+std::to_string(r));}
class UploadFixture {
public:
  Context& c;
  VkCommandPool pool{};VkCommandBuffer command{};VkFence fence{};
  VkBuffer readback{};VkDeviceMemory memory{};VkDeviceSize allocation=0;bool coherent=false;
  UploadFixture(Context& context):c(context) {
    VkCommandPoolCreateInfo p{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};p.queueFamilyIndex=c.graphics_family;
    Require(c.f.vkCreateCommandPool(c.device,&p,nullptr,&pool),"Command pool");
    VkCommandBufferAllocateInfo a{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};a.commandPool=pool;a.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;a.commandBufferCount=1;
    Require(c.f.vkAllocateCommandBuffers(c.device,&a,&command),"Command allocation");
    VkFenceCreateInfo f{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};Require(c.f.vkCreateFence(c.device,&f,nullptr,&fence),"Fence");
    VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};b.size=4096;b.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;b.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    Require(c.f.vkCreateBuffer(c.device,&b,nullptr,&readback),"Readback buffer");
    VkMemoryRequirements r{};c.f.vkGetBufferMemoryRequirements(c.device,readback,&r);allocation=r.size;
    std::vector<VkMemoryPropertyFlags> flags;
    for(uint32_t i=0;i<c.memory.memoryTypeCount;++i) flags.push_back(c.memory.memoryTypes[i].propertyFlags);
    auto type=ChooseMemoryType(r.memoryTypeBits,flags,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if(!type) throw std::runtime_error("Host-visible readback memory unavailable");
    coherent=flags[*type]&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkMemoryAllocateInfo m{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};m.allocationSize=allocation;m.memoryTypeIndex=*type;
    Require(c.f.vkAllocateMemory(c.device,&m,nullptr,&memory),"Readback memory");Require(c.f.vkBindBufferMemory(c.device,readback,memory,0),"Readback bind");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Require(c.f.vkBeginCommandBuffer(command,&begin),"Begin upload");
  }
  ~UploadFixture() {
    c.f.vkDeviceWaitIdle(c.device);
    if(readback) c.f.vkDestroyBuffer(c.device,readback,nullptr);
    if(memory) c.f.vkFreeMemory(c.device,memory,nullptr);
    if(fence) c.f.vkDestroyFence(c.device,fence,nullptr);
    if(pool) c.f.vkDestroyCommandPool(c.device,pool,nullptr);
  }
  void CopyTexture(const TextureResource& t,uint32_t dimension,VkDeviceSize offset) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=t.handle;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,t.layers};b.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    c.f.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
    VkBufferImageCopy copy{};copy.bufferOffset=offset;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,t.layers};copy.imageExtent=t.extent;
    c.f.vkCmdCopyImageToBuffer(command,t.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback,1,&copy);
    b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    c.f.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);
  }
  std::vector<uint8_t> Finish() {
    VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.buffer=readback;b.size=VK_WHOLE_SIZE;
    c.f.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&b,0,nullptr);
    Require(c.f.vkEndCommandBuffer(command),"End upload");
    VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};s.commandBufferCount=1;s.pCommandBuffers=&command;
    Require(c.f.vkQueueSubmit(c.graphics_queue,1,&s,fence),"Submit upload");
    Require(c.f.vkWaitForFences(c.device,1,&fence,VK_TRUE,10'000'000'000ull),"Upload completion");
    void* mapped=nullptr;Require(c.f.vkMapMemory(c.device,memory,0,VK_WHOLE_SIZE,0,&mapped),"Map readback");
    if(!coherent) {VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};r.memory=memory;r.size=VK_WHOLE_SIZE;auto status=c.f.vkInvalidateMappedMemoryRanges(c.device,1,&r);if(status!=VK_SUCCESS) {c.f.vkUnmapMemory(c.device,memory);Require(status,"Invalidate readback");}}
    std::vector<uint8_t> bytes(4096);std::memcpy(bytes.data(),mapped,bytes.size());c.f.vkUnmapMemory(c.device,memory);return bytes;
  }
};
void CheckTargetClears(Context& c) {
  Error e;UploadFixture fixture(c);ImageState state(c.f);TargetStore targets(c,state);
  Require(targets.BeginSubmission(fixture.command,1,e),e);
  guest::SurfaceDesc color{501,false,{16,16,0,0,0,1}};
  guest::SurfaceDesc depth{502,true,{16,16,64,0,0,1}};
  guest::ClearPacket clear{};clear.flags=0x31;clear.colors[0]=color;clear.depth_surface=depth;
  clear.color={1,0,0,1};clear.depth=0;clear.stencil=17;
  Require(targets.Clear(clear,e),e);
  clear.color={0,1,0,1};clear.depth=1;clear.stencil=93;clear.rects={{4,4,8,8}};
  Require(targets.Clear(clear,e),e);
  auto copy=[&](const guest::SurfaceDesc& description,VkImageAspectFlagBits aspect,VkDeviceSize offset) {
    auto id=targets.Acquire(description,e);Require(bool(id),e);auto t=targets.Get(id,e);Require(bool(t),e);
    Require(state.Transition(fixture.command,t->image,{t->aspects,0,1,0,1},ImageUsage::TransferSource(),e),e);
    VkBufferImageCopy region{};region.bufferOffset=offset;region.imageSubresource={VkImageAspectFlags(aspect),0,0,1};region.imageExtent={16,16,1};
    c.f.vkCmdCopyImageToBuffer(fixture.command,t->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,1,&region);
  };
  copy(color,VK_IMAGE_ASPECT_COLOR_BIT,0);copy(depth,VK_IMAGE_ASPECT_DEPTH_BIT,1024);copy(depth,VK_IMAGE_ASPECT_STENCIL_BIT,2048);
  auto bytes=fixture.Finish();targets.Retire(1);
  for(uint32_t y=0;y<16;++y) for(uint32_t x=0;x<16;++x) {
    bool inside=x>=4 && x<8 && y>=4 && y<8;size_t pixel=y*16+x;
    const uint8_t expected[]{uint8_t(inside?0:255),uint8_t(inside?255:0),0,255};
    if(std::memcmp(bytes.data()+pixel*4,expected,4)) throw std::runtime_error("Regional color clear changed an unexpected pixel");
    uint32_t d=0;std::memcpy(&d,bytes.data()+1024+pixel*4,4);
    if((d&0xffffff)!=(inside?0xffffffu:0u)) throw std::runtime_error("Regional D24 clear mismatch");
    if(bytes[2048+pixel]!=(inside?93:17)) throw std::runtime_error("Regional stencil clear mismatch");
  }
  std::cout<<"Vulkan production TargetStore regional color/D24/stencil clears passed\n";
}
int main(int argc,char** argv) {
  try {
    Win32Window window;Context c;Error e;
    c.logger=[](const std::string& s){std::cout<<s<<'\n';};
    const char* extensions[]{VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    Require(c.CreateInstance(extensions,true,e),e);Require(window.Open(64,64,e,false),e);
    auto surface=window.CreateSurface(c,e);Require(bool(surface),e);Require(c.OpenDevice(surface,"",e),e);
    if(argc==2 && std::string(argv[1])=="--targets") {CheckTargetClears(c);return c.validation_errors.load()?1:0;}
    UploadFixture fixture(c);ResourceStore store(c);Require(store.BeginSubmission(fixture.command,1,e),e);
    std::array<uint32_t,4> vertex{0x12345678,0x0017000B,0x001D0013,0x001F0007};
    Require(store.UploadBuffer(7,std::as_bytes(std::span(vertex)),1,e),e);
    auto buffer=store.Buffer(7,e);Require(bool(buffer),e);
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=buffer->handle;barrier.size=VK_WHOLE_SIZE;
    c.f.vkCmdPipelineBarrier(fixture.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&barrier,0,nullptr);
    VkBufferCopy copy{0,0,sizeof(vertex)};c.f.vkCmdCopyBuffer(fixture.command,buffer->handle,fixture.readback,1,&copy);
    std::vector<std::pair<size_t,std::vector<uint8_t>>> expected;
    for(uint32_t dimension=1;dimension<=3;++dimension) {
      guest::LinearTexture t;t.width=t.height=2;t.dimension=dimension;t.depth=dimension==1?1:(dimension==2?4:6);t.format=guest::LinearFormat::kRGBA8Unorm;
      for(uint32_t z=0;z<t.depth;++z) {t.levels.push_back({2,2,8,2,t.data.size()});for(uint32_t pixel=0;pixel<4;++pixel) t.data.insert(t.data.end(),{uint8_t(20+z),uint8_t(pixel),uint8_t(dimension),255});}
      Require(store.UploadTexture(100+dimension,t,1,e),e);auto texture=store.Texture(100+dimension,e);Require(bool(texture),e);
      size_t offset=256*dimension;fixture.CopyTexture(*texture,dimension,offset);expected.emplace_back(offset,t.data);
    }
    auto bytes=fixture.Finish();store.Retire(1);
    if(std::memcmp(bytes.data(),vertex.data(),sizeof(vertex))) throw std::runtime_error("Vertex buffer readback mismatch");
    for(auto& [offset,data]:expected) if(std::memcmp(bytes.data()+offset,data.data(),data.size())) throw std::runtime_error("2D/3D/cube upload readback mismatch");
    std::cout<<"Vulkan production ResourceStore buffer/2D/3D/cube readbacks passed; validation_errors="<<c.validation_errors.load()<<'\n';
    return c.validation_errors.load()?1:0;
  } catch(const std::exception& ex) {std::cerr<<"ERROR "<<ex.what()<<'\n';return 1;}
}
