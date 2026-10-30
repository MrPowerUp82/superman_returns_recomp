// Project-owned GPU fixtures: upload bytes through the production store and
// read them back after a real fence. This executable does not render the game.
#include "resources.h"
#include "render_targets.h"
#include "game_pipeline.h"
#include "game_renderer.h"
#include "game_frame.h"
#include "immediate.h"
#include "resolve.h"
#include "depth_resolve.h"
#include "composition.h"
#include "triangle.h"
#include "platform/win32_surface.h"
#include "../guest/texture_binding_policy.h"
#include "../../native_renderer/pm4_mirror.h"
#include "../../native_renderer/game_profile.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <chrono>
#include <thread>
#include <bit>
#include <cstdlib>
using namespace superman_returns::graphics::vulkan;
namespace guest=superman_returns::graphics::guest;
namespace shaders=superman_returns::graphics::shaders;
void Require(bool ok,const Error& e) {if(!ok) throw std::runtime_error(e.operation+": "+e.message);}
void Require(VkResult r,const char* operation) {if(r!=VK_SUCCESS) throw std::runtime_error(std::string(operation)+" result="+std::to_string(r));}
class UploadFixture {
public:
  Context& c;
  VkCommandPool pool{};VkCommandBuffer command{},uploads{};VkFence fence{};
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
  // A second command buffer submitted ahead of `command`, as GameFrame does.
  VkCommandBuffer BeginUploads() {
    VkCommandBufferAllocateInfo a{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};a.commandPool=pool;a.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;a.commandBufferCount=1;
    Require(c.f.vkAllocateCommandBuffers(c.device,&a,&uploads),"Upload command allocation");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Require(c.f.vkBeginCommandBuffer(uploads,&begin),"Begin uploads");return uploads;
  }
  std::vector<uint8_t> Finish() {
    VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.buffer=readback;b.size=VK_WHOLE_SIZE;
    c.f.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&b,0,nullptr);
    Require(c.f.vkEndCommandBuffer(command),"End upload");
    if(uploads) Require(c.f.vkEndCommandBuffer(uploads),"End uploads");
    const VkCommandBuffer commands[]{uploads,command};
    VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};s.commandBufferCount=uploads?2:1;s.pCommandBuffers=uploads?commands:&command;
    Require(c.f.vkQueueSubmit(c.graphics_queue,1,&s,fence),"Submit upload");
    Require(c.f.vkWaitForFences(c.device,1,&fence,VK_TRUE,10'000'000'000ull),"Upload completion");
    void* mapped=nullptr;Require(c.f.vkMapMemory(c.device,memory,0,VK_WHOLE_SIZE,0,&mapped),"Map readback");
    if(!coherent) {VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};r.memory=memory;r.size=VK_WHOLE_SIZE;auto status=c.f.vkInvalidateMappedMemoryRanges(c.device,1,&r);if(status!=VK_SUCCESS) {c.f.vkUnmapMemory(c.device,memory);Require(status,"Invalidate readback");}}
    std::vector<uint8_t> bytes(4096);std::memcpy(bytes.data(),mapped,bytes.size());c.f.vkUnmapMemory(c.device,memory);return bytes;
  }
};
// Each phase submits and waits a real fence; no descriptor/external owners survive
// the positive reuse phase. Copy every mip and restore the upload store's layout.
void CheckTextureUpdates(Context& c,bool separated=false) {
  const char* flag=std::getenv("SR_NATIVE_TEXTURE_UPDATES");const bool enabled=flag && std::string(flag)=="1";
  Error e;ResourceStore store(c);
  auto expect=[](bool ok,const char* message) {if(!ok) throw std::runtime_error(message);};
  guest::LinearTexture t;t.width=t.height=4;t.mip_levels=3;t.format=guest::LinearFormat::kRGBA8Unorm;
  for(uint32_t mip=0;mip<3;++mip) {uint32_t w=4>>mip;t.levels.push_back({w,w,w*4,w,t.data.size()});t.data.insert(t.data.end(),w*w*4,uint8_t(30+mip));}
  auto copy=[&](UploadFixture& f,const TextureResource& image,size_t offset) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image.handle;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,image.mips,0,1};
    b.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    c.f.vkCmdPipelineBarrier(f.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
    std::vector<VkBufferImageCopy> regions;
    for(uint32_t mip=0;mip<image.mips;++mip) {VkBufferImageCopy r{};r.bufferOffset=offset;r.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,mip,0,1};r.imageExtent={image.extent.width>>mip,image.extent.height>>mip,1};offset+=r.imageExtent.width*r.imageExtent.height*4;regions.push_back(r);}
    c.f.vkCmdCopyImageToBuffer(f.command,image.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,f.readback,uint32_t(regions.size()),regions.data());
    b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    c.f.vkCmdPipelineBarrier(f.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
  };
  auto pixels=[&](const std::vector<uint8_t>& bytes,size_t offset,const std::vector<uint8_t>& wanted) {expect(!std::memcmp(bytes.data()+offset,wanted.data(),wanted.size()),"texture update GPU mip pixels mismatch");};
  VkImage previous{};
  // Pending versions are immutable, including on the hoisted upload command.
  {
    UploadFixture f(c);Require(store.BeginSubmission(f.command,1,e,separated?f.BeginUploads():VK_NULL_HANDLE),e);
    Require(store.UploadTexture(812,t,1,e),e);auto old=store.Texture(812,e);const auto original=t.data;
    expect(!old->upload_snapshot,"immutable first upload consumed the snapshot budget");
    t.data[64]=99;Require(store.UploadTexture(812,t,2,e),e);const auto& now=*store.FindTexture(812);
    expect(old->handle!=now->handle,"pending version overwritten");previous=now->handle;
    expect(bool(now->upload_snapshot)==enabled,"changed texture snapshot admission mismatch");
    store.FinishUploads();copy(f,*old,0);copy(f,*now,256);auto bytes=f.Finish();pixels(bytes,0,original);pixels(bytes,256,t.data);store.Retire(1);
  }
  store.TakeTextureUpdateStats();
  // RED baseline: the old implementation always allocates here, despite retirement.
  {
    UploadFixture f(c);Require(store.BeginSubmission(f.command,2,e,separated?f.BeginUploads():VK_NULL_HANDLE),e);
    t.data[65]=100;Require(store.UploadTexture(812,t,3,e),e);
    expect(((*store.FindTexture(812))->handle==previous)==enabled,"retired map-only texture must reuse its image when enabled");
    const auto stats=store.TakeTextureUpdateStats();
    expect(stats.uploads==1 && stats.resource_reuses==(enabled?1u:0u) && stats.mips_skipped==(enabled?2u:0u) &&
      stats.mips_uploaded==(enabled?1u:3u) && stats.upload_bytes==(enabled?16u:84u),"selective mip upload counters mismatch");
    previous=(*store.FindTexture(812))->handle;store.FinishUploads();copy(f,**store.FindTexture(812),0);pixels(f.Finish(),0,t.data);store.Retire(2);
  }
  // External leases prevent mutation even after the submission fence retires.
  auto lease=*store.FindTexture(812);const auto leased_bytes=t.data;
  {
    UploadFixture f(c);Require(store.BeginSubmission(f.command,3,e,separated?f.BeginUploads():VK_NULL_HANDLE),e);
    t.data[66]=101;Require(store.UploadTexture(812,t,4,e),e);expect((*store.FindTexture(812))->handle!=lease->handle,"external owner overwritten");
    store.FinishUploads();copy(f,*lease,0);copy(f,**store.FindTexture(812),256);auto bytes=f.Finish();pixels(bytes,0,leased_bytes);pixels(bytes,256,t.data);store.Retire(3);
  }
  lease.reset();
  {UploadFixture f(c);Require(store.BeginSubmission(f.command,4,e),e);Require(store.AliasUploadedTexture(813,*store.FindTexture(812),e),e);f.Finish();store.Retire(4);}
  {
    UploadFixture f(c);Require(store.BeginSubmission(f.command,5,e,separated?f.BeginUploads():VK_NULL_HANDLE),e);auto original=t.data;auto alias=*store.FindTexture(813);
    t.data[67]=102;Require(store.UploadTexture(812,t,5,e),e);expect((*store.FindTexture(812))->handle!=alias->handle,"aliased image overwritten");
    store.FinishUploads();copy(f,*alias,0);copy(f,**store.FindTexture(812),256);auto bytes=f.Finish();pixels(bytes,0,original);pixels(bytes,256,t.data);store.Retire(5);
  }
  // View mapping and mip layout changes must allocate; same version keeps its pixels.
  {
    UploadFixture f(c);Require(store.BeginSubmission(f.command,6,e,separated?f.BeginUploads():VK_NULL_HANDLE),e);previous=(*store.FindTexture(812))->handle;
    t.swizzle={2,1,0,3};Require(store.UploadTexture(812,t,6,e),e);expect((*store.FindTexture(812))->handle!=previous,"view mapping change reused image");
    auto original=t.data;t.data[0]=222;previous=(*store.FindTexture(812))->handle;
    store.TakeTextureUpdateStats();
    Require(store.UploadTexture(812,t,6,e),e);expect((*store.FindTexture(812))->handle==previous,"same version replaced image");
    const auto same=store.TakeTextureUpdateStats();
    expect(!same.uploads && !same.upload_bytes && !same.mips_uploaded && !same.mips_skipped && !same.resource_reuses,"same-version upload must leave every texture update counter zero");
    expect(!store.UploadTexture(812,t,5,e),"stale texture version accepted");store.FinishUploads();copy(f,**store.FindTexture(812),0);pixels(f.Finish(),0,original);store.Retire(6);t.data=original;
  }
  {
    UploadFixture f(c);Require(store.BeginSubmission(f.command,7,e,separated?f.BeginUploads():VK_NULL_HANDLE),e);previous=(*store.FindTexture(812))->handle;
    t.mip_levels=2;t.levels.resize(2);t.data.resize(80);Require(store.UploadTexture(812,t,7,e),e);expect((*store.FindTexture(812))->handle!=previous,"mip layout change reused image");
    store.FinishUploads();copy(f,**store.FindTexture(812),0);pixels(f.Finish(),0,t.data);store.Retire(7);
  }
  {
    store.TakeTextureUpdateStats();UploadFixture f(c);Require(store.BeginSubmission(f.command,8,e,separated?f.BeginUploads():VK_NULL_HANDLE),e);previous=(*store.FindTexture(812))->handle;
    Require(store.UploadTexture(812,t,8,e),e);const auto stats=store.TakeTextureUpdateStats();
    expect(stats.resource_reuses==(enabled?1u:0u) && stats.mips_skipped==(enabled?2u:0u) && stats.upload_bytes==(enabled?0u:80u),"identical bytes with a new version must skip all mip uploads");
    expect(((*store.FindTexture(812))->handle==previous)==enabled,"identical new version reuse mismatch");
    store.FinishUploads();copy(f,**store.FindTexture(812),0);pixels(f.Finish(),0,t.data);store.Retire(8);
  }
  // Invalid plans cannot mutate the image, advance its version, or count an upload.
  {
    store.TakeTextureUpdateStats();UploadFixture f(c);Require(store.BeginSubmission(f.command,9,e,separated?f.BeginUploads():VK_NULL_HANDLE),e);
    previous=(*store.FindTexture(812))->handle;auto invalid=t;invalid.format=static_cast<guest::LinearFormat>(0x7fffffff);
    expect(!store.UploadTexture(812,invalid,9,e),"unknown format accepted");
    invalid=t;invalid.data.pop_back();expect(!store.UploadTexture(812,invalid,9,e),"truncated subresource accepted");
    const auto stats=store.TakeTextureUpdateStats();
    expect((*store.FindTexture(812))->handle==previous && (*store.FindTexture(812))->version==8 &&
      !stats.uploads && !stats.upload_bytes && !stats.mips_uploaded && !stats.mips_skipped && !stats.resource_reuses,"invalid upload changed texture or counters");
    store.FinishUploads();copy(f,**store.FindTexture(812),0);pixels(f.Finish(),0,t.data);store.Retire(9);
  }
  // Eight MiB of pixels plus snapshot metadata exceeds the per-resource cap.
  // Even with a completed fence and exclusive map ownership, both versions must
  // upload in full into distinct allocations because no exact baseline exists.
  guest::LinearTexture large;large.width=2048;large.height=1024;large.format=guest::LinearFormat::kRGBA8Unorm;
  large.levels={{large.width,large.height,large.width*4,large.height,0}};
  large.data.assign(size_t(large.width)*large.height*4,41);
  for(uint64_t serial=10;serial<=11;++serial) {
    store.TakeTextureUpdateStats();UploadFixture f(c);Require(store.BeginSubmission(f.command,serial,e,separated?f.BeginUploads():VK_NULL_HANDLE),e);
    if(serial==11) large.data[0]=77;
    Require(store.UploadTexture(814,large,serial,e),e);const auto& image=*store.FindTexture(814);
    expect(!image->upload_snapshot,"oversized upload retained an unbounded snapshot");
    if(serial==11) expect(image->handle!=previous,"missing snapshot allowed in-place reuse");
    previous=image->handle;const auto stats=store.TakeTextureUpdateStats();
    expect(stats.uploads==1 && stats.upload_bytes==large.data.size() && stats.mips_uploaded==1 &&
      !stats.mips_skipped && !stats.resource_reuses,"missing snapshot did not take the complete upload fallback");
    store.FinishUploads();
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image->handle;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    b.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    c.f.vkCmdPipelineBarrier(f.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
    std::array<VkBufferImageCopy,2> ends{};
    for(uint32_t i=0;i<2;++i) {ends[i].bufferOffset=i*4;ends[i].imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};ends[i].imageExtent={1,1,1};}
    ends[1].imageOffset={int32_t(large.width-1),int32_t(large.height-1),0};
    c.f.vkCmdCopyImageToBuffer(f.command,image->handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,f.readback,2,ends.data());
    b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    c.f.vkCmdPipelineBarrier(f.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
    auto bytes=f.Finish();expect(!std::memcmp(bytes.data(),large.data.data(),4) && !std::memcmp(bytes.data()+4,large.data.data()+large.data.size()-4,4),"snapshot fallback GPU endpoint pixels mismatch");store.Retire(serial);
  }
  std::cout<<"Texture updates enabled="<<enabled<<" separated="<<separated<<" immutable versions, retired reuse, mip pixels, leases, aliases, layout guards and snapshot fallback passed\n";
}
void BenchmarkMappedBindings(Context& c) {
  Error e;UploadFixture fixture(c);ResourceStore store(c);
  Require(store.BeginSubmission(fixture.command,1,e),e);
  guest::DrawPacket draw;
  for(uint32_t i=0;i<8;++i) {draw.texture_fetch[i][0]=2;draw.texture_fetch[i][1]=0x1006+i*4096;draw.texture_fetch[i][5]=1<<9;}
  for(uint32_t i=0;i<4;++i) {guest::VertexStream stream;stream.stream=i;stream.update.plan.key=500+i;draw.streams.push_back(stream);}
  for(uint32_t i=0;i<16;++i) draw.vertex_fetch[i]={i%4,i*4,64,37};
  constexpr uint32_t draws=3000,rounds=16;
  std::vector<TransientSlice> slices(draws);
  std::vector<guest::DrawPacket> packets(draws,draw);
  for(uint32_t i=0;i<draws;++i) {packets[i].constants.vs[0]=i*7/10;packets[i].constants.ps[0]=i*4/10;}
  const auto map_begin=std::chrono::steady_clock::now();
  for(auto& slice:slices) Require(store.MapTransient(sizeof(guest::ConstantSnapshot),slice,e),e);
  const double map_us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-map_begin).count()/draws;
  DrawBindings bindings;
  for(uint32_t i=0;i<draws;++i) Require(BuildBindings(packets[i],slices[i].data,bindings,e),e);
  const auto begin=std::chrono::steady_clock::now();
  uint64_t checksum=0;
  for(uint32_t round=0;round<rounds;++round) for(uint32_t i=0;i<draws;++i) {
    Require(BuildBindings(packets[i],slices[i].data,bindings,e),e);checksum+=bindings.textures[0][0];
  }
  const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count()/(draws*rounds);
  const auto cache_begin=std::chrono::steady_clock::now();uint64_t cached_checksum=0;
  for(uint32_t round=0;round<rounds;++round) {
    ConstantBankCache cache;
    for(uint32_t i=0;i<draws;++i) {
      auto reuse=cache.Find(1,slices[i].chunk.get(),packets[i].constants);
      Require(BuildBindings(packets[i],slices[i].data,bindings,e,(reuse.vs?0u:1u)|(reuse.ps?0u:2u)),e);
      cache.Commit(1,slices[i].chunk.get(),uint32_t(slices[i].offset),packets[i].constants,reuse);
      cached_checksum+=bindings.textures[0][0];
    }
  }
  const double cached_us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-cache_begin).count()/(draws*rounds);
  if(checksum!=cached_checksum) throw std::runtime_error("Bindings benchmark checksum differs");
  for(auto& slice:slices) Require(store.FlushTransient(slice,e),e);
  fixture.Finish();store.Retire(1);
  std::cout<<"mapped bindings flags="<<slices[0].chunk->memory_properties<<" map_us/draw="<<map_us<<" bindings_us/draw="<<us<<" cached_us/draw="<<cached_us<<" checksum="<<checksum<<'\n';
}
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
void CheckTargetDraws(Context& c,bool record=false,bool merged=false) {
  
  Error e;UploadFixture fixture(c);ImageState direct_state(c.f);TargetStore direct_targets(c,direct_state);DescriptorStore descriptors(c);GamePipelineStore pipelines(c);
  auto vertex=std::make_shared<shaders::CompiledShader>(),pixel=std::make_shared<shaders::CompiledShader>();pixel->stage=shaders::ShaderStage::kPixel;
  Require(ReadSpirv(std::filesystem::path(SR_TARGET_SHADER_DIR)/"vs.spv",vertex->words,e),e);Require(ReadSpirv(std::filesystem::path(SR_TARGET_SHADER_DIR)/"ps.spv",pixel->words,e),e);
  bool shader_pending=record;
  if(merged) _putenv_s("SR_VULKAN_CHECK_ALIASES","1");  // read when the renderer is constructed
  GameRenderer renderer(c,[&](const guest::ShaderCapture& capture){return shader_pending?shaders::ShaderResult{}:shaders::ShaderResult{shaders::ShaderPoll::ready,capture.vertex?vertex:pixel,{}};});
  auto& targets=record?renderer.Targets():direct_targets;auto& state=record?renderer.Images():direct_state;
  if(record) {Require(renderer.Initialize({},e),e);Require(renderer.BeginSubmission(fixture.command,1,e,merged?fixture.BeginUploads():VK_NULL_HANDLE),e);}
  else {Require(descriptors.Initialize(e),e);Require(pipelines.Initialize(descriptors.Layouts(),{},e),e);Require(targets.BeginSubmission(fixture.command,1,e),e);}
  guest::DrawPacket draw;draw.colors[0]={601,false,{16,16,0,0,0,1}};draw.colors[1]={602,false,{16,16,32,0,0,1}};draw.depth={603,true,{16,16,64,0,0,1}};
  draw.registers[0x104]=0xff;draw.registers[0x201]=1|(1<<16);draw.registers[0x200]=1|2|4|(1<<4)|(2<<8);draw.registers[0x10d]=7|(255<<8)|(255<<16);
  guest::ClearPacket clear{};clear.colors=draw.colors;clear.depth_surface=draw.depth;clear.flags=0x33;clear.color={1,0,0,1};clear.depth=0.25f;clear.stencil=7;Require(targets.Clear(clear,e),e);
  clear.flags=0x30;clear.rects={{4,4,8,8}};clear.depth=0.75f;Require(targets.Clear(clear,e),e);
  clear.rects={{10,10,14,14}};clear.stencil=8;Require(targets.Clear(clear,e),e);
  auto plan=PlanAttachments(draw,e);Require(e.message.empty(),e);auto pass=targets.PreparePass(plan,e);Require(bool(pass),e);
  if(record) {
    auto vs=std::make_shared<guest::ShaderCapture>(),ps=std::make_shared<guest::ShaderCapture>();vs->vertex=true;
    draw.vertex_shader=vs;draw.pixel_shader=ps;draw.count=3;draw.command_serial=1;
    guest::VertexStream stream{};stream.stream=7;stream.stride=4;stream.size=16;stream.update.plan.key=707;stream.update.plan.size=16;stream.update.plan.action=2;stream.update.plan.end=16;stream.update.bytes.assign(16,42);draw.streams={stream};
    if(renderer.Record(guest::RenderPacket(draw),fixture.command,e) || e.result!=VK_NOT_READY) throw std::runtime_error("Pending shader did not produce explicit pending state");
    shader_pending=false;draw.streams[0].update.plan.action=0;draw.streams[0].update.bytes.clear();draw.command_serial=2;
    Require(renderer.Record(guest::RenderPacket(draw),fixture.command,e),e);
    if(renderer.Stats().draws!=1 || renderer.Stats().failed || renderer.Stats().pending!=1) throw std::runtime_error("GameRenderer failed to preserve uploads across shader pending state");
    if(merged) {
      // Same targets: shares the open render pass. Depth LESS rejects every
      // pixel the first draw wrote, so the expected readback is unchanged.
      draw.command_serial=3;Require(renderer.Record(guest::RenderPacket(draw),fixture.command,e),e);
      if(renderer.Stats().draws!=2 || renderer.Stats().failed) throw std::runtime_error("Merged GameRenderer draw failed");
      // The third draw has the same inputs as the second: the pipeline memo answers it. The open pass needed no alias plan.
      if(renderer.PipelineStats().plans!=1 || renderer.PipelineStats().memo_hits!=1) throw std::runtime_error("Pipeline memo did not answer the repeated draw (plans="+std::to_string(renderer.PipelineStats().plans)+", hits="+std::to_string(renderer.PipelineStats().memo_hits)+")");
      if(renderer.Stats().alias_violations) throw std::runtime_error("Alias skip assumption violated by a repeated draw on the open pass");
      // A write to an overlapping surface of another format while the pass is open is exactly what the skip assumes
      // cannot happen. With the check on, the draw must fail loudly instead of recording a stale target.
      auto other=targets.Acquire(guest::SurfaceDesc{604,false,{16,16,0,12,0,1}},e);Require(other!=0,e);targets.MarkWritten(other);
      draw.command_serial=4;
      if(renderer.Record(guest::RenderPacket(draw),fixture.command,e) || renderer.Stats().alias_violations!=1) throw std::runtime_error("SR_VULKAN_CHECK_ALIASES did not report alias work on an open pass");
    }
    if(renderer.Record(guest::RenderPacket{},fixture.command,e) || e.message.find("empty draw payload")==std::string::npos)
      throw std::runtime_error("Default packet did not reject its empty draw payload");
    renderer.FinishSubmission();
  } else {
  auto pipeline=pipelines.Acquire(draw,*pass,*vertex,pixel.get(),1,e);Require(bool(pipeline),e);
  VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass->render_pass;begin.framebuffer=pass->framebuffer;begin.renderArea={{0,0},pass->extent};
  c.f.vkCmdBeginRenderPass(fixture.command,&begin,VK_SUBPASS_CONTENTS_INLINE);
  c.f.vkCmdBindPipeline(fixture.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline->handle);
  VkViewport viewport{0,0,16,16,0,1};VkRect2D scissor{{0,0},{16,16}};float constants[4]{};
  c.f.vkCmdSetViewport(fixture.command,0,1,&viewport);c.f.vkCmdSetScissor(fixture.command,0,1,&scissor);c.f.vkCmdSetBlendConstants(fixture.command,constants);c.f.vkCmdSetStencilReference(fixture.command,VK_STENCIL_FACE_FRONT_AND_BACK,7);
  c.f.vkCmdDraw(fixture.command,3,1,0,0);c.f.vkCmdEndRenderPass(fixture.command);targets.MarkWritten(*pass);
  }
  for(uint32_t slot=0;slot<4;++slot) {
    auto description=slot<2?draw.colors[slot]:draw.depth;auto id=targets.Acquire(description,e);auto t=targets.Get(id,e);Require(bool(t),e);
    Require(state.Transition(fixture.command,t->image,{t->aspects,0,1,0,1},ImageUsage::TransferSource(),e),e);
    VkBufferImageCopy copy{};copy.bufferOffset=slot*1024;copy.imageSubresource={VkImageAspectFlags(slot<2?VK_IMAGE_ASPECT_COLOR_BIT:slot==2?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_STENCIL_BIT),0,0,1};copy.imageExtent={16,16,1};
    c.f.vkCmdCopyImageToBuffer(fixture.command,t->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,1,&copy);
  }
  auto bytes=fixture.Finish();if(record) renderer.Retire(1);else {targets.Retire(1);pipelines.Retire(1);}
  for(uint32_t y=0;y<16;++y) for(uint32_t x=0;x<16;++x) {
    bool pass_pixel=x>=4 && x<8 && y>=4 && y<8;bool blocked=x>=10 && x<14 && y>=10 && y<14;size_t pixel=y*16+x;
    for(uint32_t slot=0;slot<2;++slot) {uint8_t expected[]{uint8_t(pass_pixel?(slot?255:0):255),uint8_t(pass_pixel?255:0),0,255};if(std::memcmp(bytes.data()+slot*1024+pixel*4,expected,4)) throw std::runtime_error("Game pipeline depth/stencil/MRT pixel mismatch");}
    uint32_t depth;std::memcpy(&depth,bytes.data()+2048+pixel*4,4);depth&=0xffffff;uint32_t expected=pass_pixel?0x800000:blocked?0xbfffff:0x400000;
    if(depth>expected+1 || depth+1<expected) throw std::runtime_error("Game pipeline depth write mismatch");
    if(bytes[3072+pixel]!=(blocked?8:7)) throw std::runtime_error("Game pipeline stencil preservation mismatch");
  }
  std::cout<<(merged?"GameRenderer merged-pass Record with hoisted uploads":record?"GameRenderer Record":"Vulkan production game pipeline")<<" depth rejection, stencil rejection, depth writes and MRT pixels passed\n";
}
void CheckResolveCopy(Context& c,bool record=false) {
  Error e;UploadFixture fixture(c);ImageState direct_state(c.f);TargetStore direct_targets(c,direct_state);ResourceStore direct_resources(c);GameRenderer renderer(c,{});
  auto& targets=record?renderer.Targets():direct_targets;auto& resources=record?renderer.Resources():direct_resources;auto& state=record?renderer.Images():direct_state;
  if(record) Require(renderer.BeginSubmission(fixture.command,1,e),e);else {Require(targets.BeginSubmission(fixture.command,1,e),e);Require(resources.BeginSubmission(fixture.command,1,e),e);}
  guest::SurfaceDesc color{801,false,{16,16,0,0,0,1}};
  guest::ClearPacket clear{};clear.flags=1;clear.colors[0]=color;clear.color={1,0,0,1};Require(targets.Clear(clear,e),e);
  clear.rects={{4,4,8,8}};clear.color={0,1,0,1};Require(targets.Clear(clear,e),e);
  guest::ResolvePacket packet{};packet.destination=900;packet.source_surface=color;
  packet.destination_fetch={2,0x100000,31u|(31u<<13)|(5u<<26),0,1u<<6,3u<<9};
  packet.level=1;packet.slice=4;packet.has_source_rect=true;packet.source={4,4,8,8};packet.has_destination_point=true;packet.destination_point={8,10};
  if(record) {packet.flags=0x100;packet.clear_color_surface=color;packet.clear_color={0,0,1,1};}
  auto plan=PlanResolve(packet,targets,e);Require(e.message.empty(),e);
  if(record) Require(renderer.Record(guest::RenderPacket(packet),fixture.command,e),e);else Require(RecordColorResolve(c,fixture.command,plan,targets,resources,state,e),e);
  // A second partial copy must preserve both the first region and other faces.
  packet.source={0,0,2,2};packet.destination_point={1,1};plan=PlanResolve(packet,targets,e);Require(e.message.empty(),e);
  if(record) Require(renderer.Record(guest::RenderPacket(packet),fixture.command,e),e);else Require(RecordColorResolve(c,fixture.command,plan,targets,resources,state,e),e);
  auto texture=resources.Texture(record?TextureResourceId(packet.destination_fetch):900,e);Require(bool(texture),e);
  Require(state.Transition(fixture.command,texture->handle,{VK_IMAGE_ASPECT_COLOR_BIT,1,1,4,1},ImageUsage::TransferSource(),e),e);
  VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,1,4,1};copy.imageExtent={16,16,1};
  c.f.vkCmdCopyImageToBuffer(fixture.command,texture->handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,1,&copy);
  Require(state.Transition(fixture.command,texture->handle,{VK_IMAGE_ASPECT_COLOR_BIT,1,1,3,1},ImageUsage::TransferSource(),e),e);
  copy.bufferOffset=1024;copy.imageSubresource.baseArrayLayer=3;c.f.vkCmdCopyImageToBuffer(fixture.command,texture->handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,1,&copy);
  if(record) {auto source=targets.Get(targets.Acquire(color,e),e);Require(bool(source),e);Require(state.Transition(fixture.command,source->image,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},ImageUsage::TransferSource(),e),e);copy.bufferOffset=2048;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.f.vkCmdCopyImageToBuffer(fixture.command,source->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,1,&copy);}
  auto bytes=fixture.Finish();if(record) renderer.Retire(1);else {resources.Retire(1);targets.Retire(1);}
  for(uint32_t y=0;y<16;++y) for(uint32_t x=0;x<16;++x) {
    bool green=x>=8&&x<12&&y>=10&&y<14,red=x>=1&&x<3&&y>=1&&y<3;
    uint8_t expected[]{uint8_t(red?255:0),uint8_t(green?255:0),0,uint8_t((red||green)?255:0)};
    if(std::memcmp(bytes.data()+(y*16+x)*4,expected,4)) throw std::runtime_error("Partial resolve lost pixels or offsets");
    uint32_t untouched=1;std::memcpy(&untouched,bytes.data()+1024+(y*16+x)*4,4);if(untouched) throw std::runtime_error("Resolve changed another cube face");
    if(record) {bool cleared=(x>=4&&x<8&&y>=4&&y<8)||(x<2&&y<2);uint8_t src[]{uint8_t(cleared?0:255),0,uint8_t(cleared?255:0),255};if(std::memcmp(bytes.data()+2048+(y*16+x)*4,src,4)) throw std::runtime_error("Resolve clear modified an unexpected source pixel");}
  }
  std::cout<<"Production color resolve preserves partial regions, mip, cube face and untouched pixels\n";
}
void CheckDepthResolve(Context& c) {
  Error e;UploadFixture fixture(c);ImageState state(c.f);TargetStore targets(c,state);ResourceStore resources(c);DepthResolver resolver(c);
  Require(targets.BeginSubmission(fixture.command,1,e),e);Require(resources.BeginSubmission(fixture.command,1,e),e);Require(resolver.Initialize(e),e);
  guest::SurfaceDesc depth{901,true,{16,16,64,0,0,1}};
  guest::ClearPacket clear{};clear.flags=0x30;clear.depth_surface=depth;clear.depth=.25f;clear.stencil=93;Require(targets.Clear(clear,e),e);
  clear.rects={{4,4,8,8}};clear.depth=.75f;clear.stencil=17;Require(targets.Clear(clear,e),e);
  guest::ResolvePacket packet{};packet.destination=902;packet.source_surface=depth;packet.destination_fetch={2,0x100017,15u|(15u<<13),0,0,1u<<9};
  packet.has_source_rect=true;packet.source={4,4,8,8};packet.has_destination_point=true;packet.destination_point={8,10};
  auto plan=PlanResolve(packet,targets,e);Require(e.message.empty(),e);Require(resolver.Record(fixture.command,plan,903,targets,resources,state,e),e);
  for(uint32_t index=0;index<2;++index) {
    auto texture=resources.Texture(index?903:902,e);Require(bool(texture),e);
    Require(state.Transition(fixture.command,texture->handle,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},ImageUsage::TransferSource(),e),e);
    VkBufferImageCopy copy{};copy.bufferOffset=index*1024;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={16,16,1};c.f.vkCmdCopyImageToBuffer(fixture.command,texture->handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,1,&copy);
  }
  auto bytes=fixture.Finish();resolver.Retire(1);resources.Retire(1);targets.Retire(1);
  for(uint32_t y=0;y<16;++y) for(uint32_t x=0;x<16;++x) {
    bool inside=x>=8&&x<12&&y>=10&&y<14;size_t pixel=y*16+x;float d=0;std::memcpy(&d,bytes.data()+pixel*4,4);
    if(std::abs(d-(inside?.75f:0.f))>0.000001f) throw std::runtime_error("Depth resolve float/offset mismatch");
    uint32_t raw=0;std::memcpy(&raw,bytes.data()+1024+pixel*4,4);
    if(inside) {if((raw&255)!=17 || ((raw>>8)>0xc00000u || (raw>>8)<0xbffffeu)) throw std::runtime_error("Depth resolve raw D24/stencil mismatch");}
    else if(raw) throw std::runtime_error("Depth resolve changed untouched pixels");
  }
  std::cout<<"Production depth resolve preserves float depth, raw D24/stencil, offsets and untouched pixels\n";
}
void CheckEdramAlias(Context& c,bool defaults=false) {
  Error e;UploadFixture fixture(c);ImageState state(c.f);TargetStore targets(c,state);ResourceStore resources(c);DepthResolver resolver(c);
  Require(targets.BeginSubmission(fixture.command,1,e),e);Require(resources.BeginSubmission(fixture.command,1,e),e);Require(resolver.Initialize(e),e);
  guest::SurfaceDesc ldr{951,false,{16,16,0,0,0,4}},hdr{952,false,{16,16,0,12,0,4}};
  guest::ClearPacket clear{};clear.flags=1;clear.colors[0]=ldr;clear.color={1,128.f/255,64.f/255,1};Require(targets.Clear(clear,e),e);
  guest::DrawPacket draw;draw.colors[0]=hdr;draw.registers[0x104]=15;auto pass=targets.PreparePass(PlanAttachments(draw,e),e);Require(bool(pass),e);
  auto aliases=targets.Aliases(*pass);if(aliases.size()!=1 || aliases[0].second.action!=guest::AliasAction::kReinterpret) throw std::runtime_error("Expected LDR-to-HDR EDRAM alias");
  Require(resolver.RecordAlias(fixture.command,aliases[0].first,aliases[0].second,targets,resources,state,e,AliasOptions{defaults,0}),e);
  if(defaults) {
    auto image=targets.Get(aliases[0].first,e);Require(bool(image),e);Require(state.Transition(fixture.command,image->image,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},ImageUsage::TransferSource(),e),e);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={16,16,1};c.f.vkCmdCopyImageToBuffer(fixture.command,image->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,1,&copy);
    auto color=targets.Get(targets.Acquire(ldr,e),e);Require(bool(color),e);guest::AliasPlan invalidate{};invalidate.action=guest::AliasAction::kClear;
    Require(resolver.RecordAlias(fixture.command,color->id,invalidate,targets,resources,state,e),e);Require(state.Transition(fixture.command,color->image,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},ImageUsage::TransferSource(),e),e);
    copy.bufferOffset=2048;c.f.vkCmdCopyImageToBuffer(fixture.command,color->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,1,&copy);
    auto bytes=fixture.Finish();resolver.Retire(1);resources.Retire(1);targets.Retire(1);
    for(uint32_t pixel=0;pixel<256;++pixel) {uint16_t value[4]{};std::memcpy(value,bytes.data()+pixel*8,8);if(value[0] || value[1] || value[2] || value[3]!=0x3c00) throw std::runtime_error("Default LDR-to-HDR black policy differs from reference");uint32_t color_value=1;std::memcpy(&color_value,bytes.data()+2048+pixel*4,4);if(color_value) throw std::runtime_error("Default alias clear alpha differs from reference");}
    std::cout<<"Production EDRAM defaults preserve HDR-from-LDR black and alias clear alpha0\n";return;
  }
  // Round-trip the guest bit encoding, rather than merely converting RGB values.
  auto destination=targets.Get(targets.Acquire(ldr,e),e);Require(bool(destination),e);
  guest::AliasPlan back{};back.action=guest::AliasAction::kReinterpret;back.source=aliases[0].first;
  Require(resolver.RecordAlias(fixture.command,destination->id,back,targets,resources,state,e),e);
  Require(state.Transition(fixture.command,destination->image,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},ImageUsage::TransferSource(),e),e);
  VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={16,16,1};c.f.vkCmdCopyImageToBuffer(fixture.command,destination->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,1,&copy);
  auto bytes=fixture.Finish();resolver.Retire(1);resources.Retire(1);targets.Retire(1);
  for(uint32_t pixel=0;pixel<256;++pixel) {uint8_t expected[]{255,128,64,255};if(std::memcmp(bytes.data()+pixel*4,expected,4)) throw std::runtime_error("EDRAM alias failed bit-encoding round trip");}
  std::cout<<"Production EDRAM RGBA8/7e3 reinterpretation round trip passed\n";
}
void CheckResolvedDraw(Context& c,bool mask_check=false) {
  Error e;UploadFixture fixture(c);
  auto vertex=std::make_shared<shaders::CompiledShader>(),pixel=std::make_shared<shaders::CompiledShader>();pixel->stage=shaders::ShaderStage::kPixel;
  Require(ReadSpirv(std::filesystem::path(SR_TARGET_SHADER_DIR)/"vs.spv",vertex->words,e),e);Require(ReadSpirv(std::filesystem::path(SR_TARGET_SHADER_DIR)/"resolve_sample.spv",pixel->words,e),e);
  GameRenderer renderer(c,[&](const guest::ShaderCapture& s){return shaders::ShaderResult{shaders::ShaderPoll::ready,s.vertex?vertex:pixel,{}};});
  Require(renderer.Initialize({},e),e);Require(renderer.BeginSubmission(fixture.command,1,e),e);
  guest::SurfaceDesc source{981,false,{16,16,0,0,0,1}},target{982,false,{16,16,32,0,0,1}};
  guest::ClearPacket clear{};clear.flags=1;clear.colors[0]=source;clear.color={1,0,0,1};Require(renderer.Record(guest::RenderPacket(clear),fixture.command,e),e);
  guest::ResolvePacket resolve{};resolve.command_serial=1;resolve.source_surface=source;resolve.destination=983;resolve.destination_fetch={2,0x100006,15u|(15u<<13),0,0,1u<<9};
  Require(renderer.Record(guest::RenderPacket(resolve),fixture.command,e),e);
  guest::WorkBatch front_batch;front_batch.bytes={0,0x10,0,6};front_batch.ranges={{0x500020,4,0}};guest::WorkCmd front_command;front_command.range_count=1;guest::CapturedMemory front_memory;std::string capture_error;
  if(!guest::CapturedMemory::Capture(front_batch,front_command,front_memory,capture_error)) throw std::runtime_error(capture_error);
  front_batch.Clear();auto front=renderer.SelectFrontbuffer(guest::SwapPacket{0x500000,1,front_memory},e);Require(bool(front),e);
  if(renderer.SelectFrontbuffer(guest::SwapPacket{0x600000,2,{}},e) || e.message.empty()) throw std::runtime_error("Missing captured frontbuffer silently substituted another image");
  guest::DrawPacket draw;draw.colors[0]=target;draw.registers[0x104]=15;draw.registers[0x201]=1|(1<<16);draw.count=3;draw.command_serial=2;
  auto vs=std::make_shared<guest::ShaderCapture>(),ps=std::make_shared<guest::ShaderCapture>();vs->vertex=true;draw.vertex_shader=vs;draw.pixel_shader=ps;
  draw.texture_fetch[7]=resolve.destination_fetch;draw.texture_fetch[7][3]=(2u|(1u<<3)|(0u<<6)|(3u<<9))<<1;
  // No guest CPU texture capture exists: this fetch must use the GPU resolve.
  // Four frozen draws compare filtering off/on, then reintroduce a previously
  // omitted slot after the resolve changes. Read every result after a fence.
  for(uint32_t sample=0;sample<(mask_check?4u:1u);++sample) {
    if(mask_check) {
      if(sample==2) {
        clear.color={0,1,0,1};
        Require(renderer.Record(guest::RenderPacket(clear),fixture.command,e),e);
        resolve.command_serial=10;
        Require(renderer.Record(guest::RenderPacket(resolve),fixture.command,e),e);
      }
      guest::WorkBatch batch;batch.bytes.resize(0x4000,0);batch.ranges={{0x10000,0x4000,0}};
      const uint32_t fetch_offset=superman_returns::native::profile::kDevice.fetch_constants;
      for(uint32_t slot:{0u,7u}) for(uint32_t word=0;word<6;++word)
        for(uint32_t byte=0;byte<4;++byte)
          batch.bytes[fetch_offset+slot*24+word*4+byte]=uint8_t(draw.texture_fetch[7][word]>>(24-byte*8));
      vs->texture_usage={0,true};ps->texture_usage={1u<<7,true};
      guest::WorkCmd cmd;cmd.op=guest::Op::kDraw;cmd.u[0]=4;cmd.u[2]=3;
      cmd.device=0x10000;cmd.range_count=1;
      // Sample 2 changes the shader's usage as well as the resolve: slot 0 is
      // required again, and must see the new GPU bytes rather than a stale view.
      if(sample==2) ps->texture_usage.mask|=1u;
      cmd.texture_mask=guest::TextureMaskForShaders(vs.get(),ps.get(),true,sample!=0);
      superman_returns::native::Pm4Mirror mirror;guest::RenderPacket decoded;std::string error;
      if(!guest::ReplayCapturedRenderPacket(batch,cmd,mirror,decoded,error)) throw std::runtime_error(error);
      draw.texture_fetch=guest::GetDraw(decoded)->texture_fetch;
      if((sample==1 || sample==3) && draw.texture_fetch[0][0]) throw std::runtime_error("Unused slot survived captured replay mask");
      if(sample==2 && !(draw.texture_fetch[0][0]&2)) throw std::runtime_error("Used slot was not reactivated");
    }
    draw.command_serial=sample+20;
    if(mask_check) std::cout<<"texture-mask sample="<<sample<<" slot0="<<std::hex<<draw.texture_fetch[0][0]<<","<<draw.texture_fetch[0][1]<<" slot7="<<draw.texture_fetch[7][0]<<","<<draw.texture_fetch[7][1]<<std::dec<<'\n';
    Require(renderer.Record(guest::RenderPacket(draw),fixture.command,e),e);
    auto image=renderer.Targets().Get(renderer.Targets().Acquire(target,e),e);Require(bool(image),e);
    Require(renderer.Images().Transition(fixture.command,image->image,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},ImageUsage::TransferSource(),e),e);
    VkBufferImageCopy copy{};copy.bufferOffset=sample*1024;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={16,16,1};c.f.vkCmdCopyImageToBuffer(fixture.command,image->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,1,&copy);
  }
  auto bytes=fixture.Finish();renderer.Retire(1);
  for(uint32_t sample=0;sample<(mask_check?4u:1u);++sample)
    for(uint32_t pixel=0;pixel<256;++pixel) {uint8_t expected[]{0,uint8_t(sample>=2?255:0),uint8_t(sample<2?255:0),255};if(std::memcmp(bytes.data()+sample*1024+pixel*4,expected,4)) throw std::runtime_error("Resolved texture fetch/swizzle draw mismatch");}
  if(mask_check && std::memcmp(bytes.data(),bytes.data()+1024,1024)) throw std::runtime_error("Frozen masked/unmasked images differ");
  if(mask_check) std::cout<<"Frozen masked/unmasked pixels match; reactivated slot observes updated resolve\n";
  std::cout<<"GameRenderer samples GPU resolve at guest slot7 with a different fetch swizzle and no CPU upload\n";
}
// Keep every draw/copy in one submission, so comparisons must use the bytes
// actually uploaded, even when a shader previously omitted a changed register.
// A/B controls are inherited at process launch; never toggle them in this test.
void CheckConstantMask(Context& c) {
  constexpr uint32_t width=16,height=8,samples=7;
  constexpr VkDeviceSize image_bytes=width*height*4;
  static_assert(samples*image_bytes<4096,"Constant-mask copies must fit UploadFixture");
  const char* cache_env=std::getenv("SR_VULKAN_CONSTANT_BANK_CACHE");
  const bool cache=cache_env && cache_env[0] && cache_env[0]!='0';
  const bool mask=guest::ShaderConstantMaskEnabled();
  Error e;UploadFixture fixture(c);
  auto vertex=std::make_shared<shaders::CompiledShader>();
  Require(ReadSpirv(std::filesystem::path(SR_TARGET_SHADER_DIR)/"vs.spv",vertex->words,e),e);
  const std::array<const char*,3> entries{"PS0","PS1","PSDynamic"};
  std::array<std::shared_ptr<shaders::CompiledShader>,3> pixels;
  auto vs=std::make_shared<guest::ShaderCapture>();vs->vertex=true;vs->hash=0xc000;
  vs->constant_usage.known=true; // Known empty: fullscreen VS reads no constants.
  std::array<std::shared_ptr<guest::ShaderCapture>,3> ps;
  for(uint32_t i=0;i<ps.size();++i) {
    pixels[i]=std::make_shared<shaders::CompiledShader>();pixels[i]->stage=shaders::ShaderStage::kPixel;
    Require(ReadSpirv(std::filesystem::path(SR_TARGET_SHADER_DIR)/(std::string("constant_mask_")+entries[i]+".spv"),pixels[i]->words,e),e);
    ps[i]=std::make_shared<guest::ShaderCapture>();ps[i]->hash=0xc001+i;
    ps[i]->constant_usage.known=true;
    if(i<2) ps[i]->constant_usage.mask[0]=uint64_t{1}<<i;
    else {ps[i]->constant_usage.mask.fill(UINT64_MAX);ps[i]->constant_usage.dynamic=true;}
  }
  GameRenderer renderer(c,[&](const guest::ShaderCapture& capture)->shaders::ShaderResult {
    if(capture.vertex && capture.hash==vs->hash) return {shaders::ShaderPoll::ready,vertex,{}};
    // Stage alone is insufficient: each distinct capture must select its own
    // host-compiled artifact, including when returning to PS0 after PS1/dynamic.
    for(uint32_t i=0;i<ps.size();++i)
      if(!capture.vertex && capture.hash==ps[i]->hash) return {shaders::ShaderPoll::ready,pixels[i],{}};
    throw std::runtime_error("Unknown constant-mask shader capture");
  });
  renderer.EnableProfile(true);Require(renderer.Initialize({},e,false),e);
  Require(renderer.BeginSubmission(fixture.command,1,e),e);
  guest::DrawPacket draw;draw.colors[0]={2982,false,{width,height,32,0,0,1}};
  draw.registers[0x104]=15;draw.registers[0x201]=1|(1<<16);draw.count=3;
  draw.vertex_shader=vs;draw.pixel_shader=ps[0];
  guest::FloatConstantBank vs_words{},ps_words{};
  auto write=[](guest::FloatConstantBank& bank,uint32_t reg,std::array<float,4> value) {
    for(uint32_t component=0;component<4;++component) bank[reg*4+component]=std::bit_cast<uint32_t>(value[component]);
  };
  write(ps_words,0,{1,0,0,1});write(ps_words,1,{0,0,1,1});
  for(uint32_t sample=0;sample<samples;++sample) {
    switch(sample) {
    case 0: break; // PS0: red c0, blue c1.
    case 1: write(ps_words,1,{0,1,0,1});break; // Unused c1 changes: still red.
    case 2: draw.pixel_shader=ps[1];break; // Must observe green, not uploaded blue.
    case 3: draw.pixel_shader=ps[0];write(ps_words,0,{0,0,1,1});break;
    case 4: draw.pixel_shader=ps[2];write(ps_words,2,{1,0,0,0});break;
    case 5: write(ps_words,1,{1,1,0,1});break; // Dynamic c[1] must observe yellow.
    case 6:
      draw.pixel_shader=ps[0];
      write(vs_words,255,{1,2,3,4});write(ps_words,255,{4,3,2,1});break;
    }
    // Publish complete sanitized banks, just like captured production packets.
    // Do not pre-mask/zero unused registers: the renderer owns that decision.
    guest::ConstantSnapshot sanitized;std::string error;
    if(!guest::CaptureFloatConstants(vs_words,false,sanitized.vs,error) ||
       !guest::CaptureFloatConstants(ps_words,false,sanitized.ps,error)) throw std::runtime_error(error);
    draw.constants=sanitized;draw.command_serial=sample+1;
    Require(renderer.Record(guest::RenderPacket(draw),fixture.command,e),e);
    auto image=renderer.Targets().Get(renderer.Targets().Acquire(draw.colors[0],e),e);Require(bool(image),e);
    Require(renderer.Images().Transition(fixture.command,image->image,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},ImageUsage::TransferSource(),e),e);
    VkBufferImageCopy copy{};copy.bufferOffset=sample*image_bytes;
    copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};
    c.f.vkCmdCopyImageToBuffer(fixture.command,image->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,1,&copy);
  }
  renderer.FinishSubmission();auto bytes=fixture.Finish();
  const auto profile=renderer.TakeProfile();renderer.Retire(1);
  const std::array<std::array<uint8_t,4>,samples> expected{{
    {255,0,0,255},{255,0,0,255},{0,255,0,255},{0,0,255,255},
    {0,255,0,255},{255,255,0,255},{0,0,255,255}}};
  for(uint32_t sample=0;sample<samples;++sample) for(uint32_t pixel=0;pixel<width*height;++pixel)
    if(std::memcmp(bytes.data()+sample*image_bytes+pixel*4,expected[sample].data(),4))
      throw std::runtime_error("Constant-mask pixel mismatch: sample="+std::to_string(sample)+", pixel="+std::to_string(pixel));
  std::cout<<"constant-mask cache="<<cache<<" mask="<<mask<<" draws="<<profile.draws
           <<" vs_constant_reuses="<<profile.vs_constant_reuses<<" ps_constant_reuses="<<profile.ps_constant_reuses
           <<" readback_bytes="<<samples*image_bytes<<'\n';
  if(renderer.Stats().draws!=samples || renderer.Stats().failed || renderer.Stats().pending || profile.draws!=samples)
    throw std::runtime_error("Constant-mask fixture did not record all seven draws");
  // Seven 12-KiB slices fit the production 8-MiB transient chunk. Whole-bank
  // comparison reuses VS five times and PS once; masks save two more uploads.
  const uint64_t expected_vs=cache?(mask?6:5):0,expected_ps=cache?(mask?2:1):0;
  if(profile.vs_constant_reuses!=expected_vs || profile.ps_constant_reuses!=expected_ps)
    throw std::runtime_error("Constant-mask bank reuse count mismatch: expected VS="+std::to_string(expected_vs)+", PS="+std::to_string(expected_ps));
  std::cout<<"Seven production per-draw constant-bank readbacks passed (red/red/green/blue/green/yellow/blue)\n";
}
// Real point/linear samplers must remain distinct while an immutable image/view
// is shared. Record two content versions before submitting either to the GPU.
void CheckTextureStorageReuse(Context& c,bool global=false,bool merged=false) {
  Error e;UploadFixture fixture(c);uint32_t decodes=0;
  auto vertex=std::make_shared<shaders::CompiledShader>(),pixel=std::make_shared<shaders::CompiledShader>();pixel->stage=shaders::ShaderStage::kPixel;
  Require(ReadSpirv(std::filesystem::path(SR_TARGET_SHADER_DIR)/"vs.spv",vertex->words,e),e);
  if(global) {
#ifdef SR_GLOBAL_SHADER_DIR
    Require(ReadSpirv(std::filesystem::path(SR_GLOBAL_SHADER_DIR)/"texture_storage_ps.spv",pixel->words,e),e);
    vertex->abi=pixel->abi=shaders::ShaderAbi::kGlobal;
#else
    throw std::runtime_error("Global storage fixture needs the global test executable");
#endif
  } else Require(ReadSpirv(std::filesystem::path(SR_TARGET_SHADER_DIR)/"resolve_sample.spv",pixel->words,e),e);
  GameRenderer renderer(c,[&](const guest::ShaderCapture& s){return shaders::ShaderResult{shaders::ShaderPoll::ready,s.vertex?vertex:pixel,{}};},
      [&](const guest::TextureCapture& capture,guest::LinearTexture& t,std::string&) {
        ++decodes;t.width=t.height=2;t.format=guest::LinearFormat::kRGBA8Unorm;
        t.levels={{2,2,8,2,0}};
        if(capture.version==1) t.data={255,0,0,255,0,0,255,255,255,0,0,255,0,0,255,255};
        else t.data={0,255,0,255,0,255,0,255,0,255,0,255,0,255,0,255};
        return true;
      });
  Require(renderer.Initialize({},e,global),e);Require(renderer.BeginSubmission(fixture.command,1,e,merged?fixture.BeginUploads():VK_NULL_HANDLE),e);
  guest::SurfaceDesc target{1982,false,{16,16,32,0,0,1}};
  guest::DrawPacket draw;draw.colors[0]=target;draw.registers[0x104]=15;draw.registers[0x201]=1|(1<<16);draw.count=3;
  auto vs=std::make_shared<guest::ShaderCapture>(),ps=std::make_shared<guest::ShaderCapture>();vs->vertex=true;draw.vertex_shader=vs;draw.pixel_shader=ps;
  const std::array<uint32_t,6> point{2,0x100006,1u|(1u<<13),0x688,0,1u<<9};
  auto linear=point;linear[3]|=(1u<<19)|(1u<<21);
  if(TextureResourceId(point)==TextureResourceId(linear)) throw std::runtime_error("Sampler variants lost binding identity");
  auto first=std::make_shared<guest::TextureCapture>();first->fetch=point;first->version=1;
  auto second=std::make_shared<guest::TextureCapture>(*first);second->version=2;
  std::array<std::shared_ptr<TextureResource>,4> resources;
  const bool sharing=guest::TextureStorageReuseEnabled(guest::TextureStorageBackend::kVulkan);
  for(uint32_t sample=0;sample<4;++sample) {
    draw.command_serial=sample+1;draw.texture_fetch[7]=(sample==0 || sample==3)?point:linear;
    draw.textures[7]=sample<2?first:second;
    Require(renderer.Record(guest::RenderPacket(draw),fixture.command,e),e);
    resources[sample]=renderer.Resources().Texture(TextureResourceId(draw.texture_fetch[7]),e);Require(bool(resources[sample]),e);
    auto image=renderer.Targets().Get(renderer.Targets().Acquire(target,e),e);Require(bool(image),e);
    Require(renderer.Images().Transition(fixture.command,image->image,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},ImageUsage::TransferSource(),e),e);
    VkBufferImageCopy copy{};copy.bufferOffset=sample*1024;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={16,16,1};
    c.f.vkCmdCopyImageToBuffer(fixture.command,image->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,1,&copy);
  }
  if((resources[0]==resources[1])!=sharing || (resources[2]==resources[3])!=sharing || resources[0]==resources[2] ||
      decodes!=(sharing?2u:4u) || renderer.Stats().texture_storage_reuses!=(sharing?2u:0u))
    throw std::runtime_error("Storage sharing/version/decode count mismatch");
  // A different immutable snapshot with the same serial is not sufficient
  // evidence of equal bytes. Only the actual shared snapshot permits aliasing.
  auto independent=std::make_shared<guest::TextureCapture>(*second);
  auto clamp_variant=point;clamp_variant[0]|=1u<<10;
  draw.texture_fetch[7]=clamp_variant;draw.textures[7]=independent;draw.command_serial=5;
  Require(renderer.Record(guest::RenderPacket(draw),fixture.command,e),e);
  auto separate=renderer.Resources().Texture(TextureResourceId(clamp_variant),e);Require(bool(separate),e);
  if(separate==resources[2] || decodes!=(sharing?3u:5u)) throw std::runtime_error("Independent snapshots aliased by serial alone");
  // Reject stale versions and GPU-authoritative resolve images even if a caller
  // accidentally tries to feed one into the CPU-upload alias API.
  if(renderer.Resources().AliasUploadedTexture(TextureResourceId(linear),resources[0],e)) throw std::runtime_error("Stale alias accepted");
  auto resolved=renderer.Resources().ResolveTexture(1983,VK_FORMAT_R8G8B8A8_UNORM,{2,2,1},1,1,false,renderer.Images(),e);Require(bool(resolved),e);
  if(renderer.Resources().AliasUploadedTexture(1984,resolved,e)) throw std::runtime_error("Resolve accepted as CPU upload alias");
  std::weak_ptr<TextureResource> pending=resources[0];for(auto& r:resources) r.reset();
  if(pending.expired()) throw std::runtime_error("Pending upload freed before fence");
  renderer.FinishSubmission();auto bytes=fixture.Finish();renderer.Retire(1);
  const int expected[4][4]={{0,0,255,255},{128,0,128,255},{0,255,0,255},{0,255,0,255}};
  for(uint32_t sample=0;sample<4;++sample) for(uint32_t pixel=0;pixel<256;++pixel) for(uint32_t channel=0;channel<4;++channel)
    if(std::abs(int(bytes[sample*1024+pixel*4+channel])-expected[sample][channel])>1)
      throw std::runtime_error("Shared texture sampler/content readback mismatch sample="+std::to_string(sample)+" channel="+std::to_string(channel)+" actual="+std::to_string(bytes[sample*1024+pixel*4+channel]));
  std::cout<<"Texture storage sharing="<<sharing<<" global="<<global<<" merged="<<merged<<" decodes="<<decodes<<"; point/linear pixels, new content, independent snapshots and pending lifetime passed\n";
}
void CheckComposition(Context& c) {
  Error e;UploadFixture fixture(c);ImageState state(c.f);TargetStore targets(c,state);ResourceStore resources(c);FrontbufferCompositor compositor(c);
  Require(targets.BeginSubmission(fixture.command,1,e),e);Require(resources.BeginSubmission(fixture.command,1,e),e);Require(resources.CreateDummies(e),e);Require(compositor.Initialize(e),e);
  guest::SurfaceDesc source{991,false,{16,16,0,0,0,1}},target{992,false,{16,16,32,0,0,1}};
  guest::ClearPacket clear{};clear.flags=1;clear.colors[0]=source;clear.color={1,0,0,1};Require(targets.Clear(clear,e),e);
  clear.colors[0]=target;clear.color={0,0,0,1};Require(targets.Clear(clear,e),e);
  guest::ResolvePacket packet{};packet.destination=993;packet.source_surface=source;packet.destination_fetch={2,0x100006,15u|(15u<<13),0,0,1u<<9};
  auto resolve=PlanResolve(packet,targets,e);Require(e.message.empty(),e);Require(RecordColorResolve(c,fixture.command,resolve,targets,resources,state,e),e);auto image=resources.Texture(993,e);Require(bool(image),e);
  guest::DrawPacket draw;draw.colors[0]=target;draw.registers[0x104]=15;auto pass=targets.PreparePass(PlanAttachments(draw,e),e);Require(bool(pass),e);
  std::array<uint32_t,256> gamma;gamma.fill((512u<<20)|(256u<<10)|768u);
  auto composition=compositor.Prepare(fixture.command,*pass,image,resources,state,gamma,true,16,9,e);Require(bool(composition),e);
  VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass->render_pass;begin.framebuffer=pass->framebuffer;begin.renderArea={{0,0},pass->extent};
  c.f.vkCmdBeginRenderPass(fixture.command,&begin,VK_SUBPASS_CONTENTS_INLINE);compositor.Record(fixture.command,*composition);c.f.vkCmdEndRenderPass(fixture.command);
  auto output=targets.Get(targets.Acquire(target,e),e);Require(bool(output),e);Require(state.Transition(fixture.command,output->image,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},ImageUsage::TransferSource(),e),e);
  VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={16,16,1};c.f.vkCmdCopyImageToBuffer(fixture.command,output->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,1,&copy);
  auto bytes=fixture.Finish();compositor.Retire(1);resources.Retire(1);targets.Retire(1);
  for(uint32_t y=0;y<16;++y) for(uint32_t x=0;x<16;++x) {bool inside=y>=3&&y<12;int expected[]{inside?128:0,inside?64:0,inside?191:0,255};for(uint32_t channel=0;channel<4;++channel) if(std::abs(int(bytes[(y*16+x)*4+channel])-expected[channel])>1) throw std::runtime_error("Frontbuffer composition mismatch x="+std::to_string(x)+" y="+std::to_string(y)+" channel="+std::to_string(channel)+" actual="+std::to_string(bytes[(y*16+x)*4+channel])+" expected="+std::to_string(expected[channel]));}
  std::cout<<"Production frontbuffer composition gamma LUT and letterboxed pixels passed\n";
}
namespace {
std::mutex* checked_game_mutex=nullptr;
PFN_vkCmdClearAttachments original_game_clear=nullptr;
PFN_vkQueueSubmit original_game_submit=nullptr;
bool game_record_locked=false,game_submit_unlocked=false;
uint32_t observed_game_clears=0,observed_game_submits=0;
bool CanAcquireFromAnotherThread() {
  bool acquired=false;
  std::thread probe([&]{acquired=checked_game_mutex->try_lock();if(acquired) checked_game_mutex->unlock();});probe.join();return acquired;
}
void VKAPI_CALL CheckedGameClear(VkCommandBuffer command,uint32_t count,const VkClearAttachment* attachments,uint32_t rect_count,const VkClearRect* rects) {
  ++observed_game_clears;game_record_locked|=!CanAcquireFromAnotherThread();
  original_game_clear(command,count,attachments,rect_count,rects);
}
VkResult VKAPI_CALL CheckedGameSubmit(VkQueue queue,uint32_t count,const VkSubmitInfo* submissions,VkFence fence) {
  ++observed_game_submits;game_submit_unlocked|=CanAcquireFromAnotherThread();
  return original_game_submit(queue,count,submissions,fence);
}
}
void CheckGameFrame(Context& c) {
  Error e;std::mutex queue_mutex;GameFrame frame(c,queue_mutex,{},{});Require(frame.Initialize({},e),e);
  struct RestoreDispatch {Context& c;~RestoreDispatch(){c.f.vkCmdClearAttachments=original_game_clear;c.f.vkQueueSubmit=original_game_submit;}} restore{c};
  checked_game_mutex=&queue_mutex;original_game_clear=c.f.vkCmdClearAttachments;original_game_submit=c.f.vkQueueSubmit;
  game_record_locked=game_submit_unlocked=false;observed_game_clears=observed_game_submits=0;
  c.f.vkCmdClearAttachments=CheckedGameClear;c.f.vkQueueSubmit=CheckedGameSubmit;
  guest::SurfaceDesc source{1001,false,{16,16,0,0,0,1}};
  guest::WorkBatch batch;batch.bytes={0,0x10,0,6};batch.ranges={{0x500020,4,0}};guest::WorkCmd command;command.range_count=1;guest::CapturedMemory memory;std::string reason;
  if(!guest::CapturedMemory::Capture(batch,command,memory,reason)) throw std::runtime_error(reason);
  std::shared_ptr<TextureResource> first,second,unused;
  auto record=[&](bool green,std::shared_ptr<TextureResource>& output) {
    // A tile rectangle smaller than the surface must not select a separate,
    // undersized target for the full-surface clear and subsequent resolve.
    guest::PassPacket tiling{};tiling.operation=guest::Op::kBeginTiling;tiling.rects={{0,0,8,8}};tiling.clear_color=true;tiling.color_surface=source;tiling.color=green?std::array<float,4>{0,1,0,1}:std::array<float,4>{1,0,0,1};
    Require(frame.Enqueue(guest::RenderPacket(tiling),unused,e),e);
    guest::ResolvePacket resolve{};resolve.source_surface=source;resolve.destination=1002;resolve.destination_fetch={2,0x100006,15u|(15u<<13),0,0,1u<<9};Require(frame.Enqueue(guest::RenderPacket(resolve),unused,e),e);
    Require(frame.Enqueue(guest::RenderPacket(guest::SwapPacket{0x500000,1,memory}),output,e),e);Require(bool(output),e);
  };
  record(false,first);record(true,second);
  c.f.vkCmdClearAttachments=original_game_clear;c.f.vkQueueSubmit=original_game_submit;
  if(game_record_locked || game_submit_unlocked || observed_game_clears!=2 || observed_game_submits!=2)
    throw std::runtime_error("Game frame must record without the queue mutex and serialize both GPU submissions");
  if(first->handle==second->handle) throw std::runtime_error("Mailbox snapshot reuses an image still owned by the consumer");
  auto read=[&](const std::shared_ptr<TextureResource>& image,bool green) {
    UploadFixture fixture(c);Require(frame.Renderer().Images().Transition(fixture.command,image->handle,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},ImageUsage::TransferSource(),e),e);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={16,16,1};c.f.vkCmdCopyImageToBuffer(fixture.command,image->handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,1,&copy);
    auto bytes=fixture.Finish();for(uint32_t pixel=0;pixel<256;++pixel) {uint8_t expected[]{uint8_t(green?0:255),uint8_t(green?255:0),0,255};if(std::memcmp(bytes.data()+pixel*4,expected,4)) throw std::runtime_error("Game frame snapshot pixels changed after the following frame");}
  };
  read(first,false);read(second,true);first.reset();second.reset();frame.Cancel();
  if(frame.Enqueue(guest::RenderPacket(guest::SwapPacket{}),unused,e) || e.message.empty()) throw std::runtime_error("Cancelled game frame accepted work");
  std::cout<<"Game frame producer submits owned packets, tiling clears and immutable mailbox snapshots\n";
}
// Same frames as CheckGameFrame, recorded on GameFrame's async thread: the
// producer gets no output, the callback receives both frames in order.
void CheckGameFrameAsync(Context& c) {
  Error e;std::mutex queue_mutex;GameFrame frame(c,queue_mutex,{},{});Require(frame.Initialize({},e),e);
  std::mutex outputs_mutex;std::vector<std::shared_ptr<TextureResource>> outputs;
  frame.StartAsync([&](std::shared_ptr<TextureResource> image,const guest::SwapPacket&) {std::lock_guard lock(outputs_mutex);outputs.push_back(std::move(image));});
  guest::SurfaceDesc source{1001,false,{16,16,0,0,0,1}};
  guest::WorkBatch batch;batch.bytes={0,0x10,0,6};batch.ranges={{0x500020,4,0}};guest::WorkCmd command;command.range_count=1;guest::CapturedMemory memory;std::string reason;
  if(!guest::CapturedMemory::Capture(batch,command,memory,reason)) throw std::runtime_error(reason);
  std::shared_ptr<TextureResource> unused;
  for(bool green:{false,true}) {
    guest::PassPacket tiling{};tiling.operation=guest::Op::kBeginTiling;tiling.rects={{0,0,8,8}};tiling.clear_color=true;tiling.color_surface=source;tiling.color=green?std::array<float,4>{0,1,0,1}:std::array<float,4>{1,0,0,1};
    Require(frame.Enqueue(guest::RenderPacket(tiling),unused,e),e);
    guest::ResolvePacket resolve{};resolve.source_surface=source;resolve.destination=1002;resolve.destination_fetch={2,0x100006,15u|(15u<<13),0,0,1u<<9};Require(frame.Enqueue(guest::RenderPacket(resolve),unused,e),e);
    Require(frame.Enqueue(guest::RenderPacket(guest::SwapPacket{0x500000,1,memory}),unused,e),e);
    if(unused) throw std::runtime_error("Async frame returned a synchronous output");
  }
  frame.StopAsync();
  if(outputs.size()!=2 || outputs[0]->handle==outputs[1]->handle) throw std::runtime_error("Async recorder must publish two distinct snapshots");
  for(size_t i=0;i<2;++i) {
    UploadFixture fixture(c);Require(frame.Renderer().Images().Transition(fixture.command,outputs[i]->handle,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},ImageUsage::TransferSource(),e),e);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={16,16,1};c.f.vkCmdCopyImageToBuffer(fixture.command,outputs[i]->handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,1,&copy);
    auto bytes=fixture.Finish();uint8_t expected[]{uint8_t(i?0:255),uint8_t(i?255:0),0,255};
    for(uint32_t pixel=0;pixel<256;++pixel) if(std::memcmp(bytes.data()+pixel*4,expected,4)) throw std::runtime_error("Async game frame snapshot pixels mismatch");
  }
  std::cout<<"Async game frame recorder publishes ordered snapshots and drains on stop\n";
}
void CheckImmediate(Context& c) {
  Error e;UploadFixture fixture(c);ImageState state(c.f);TargetStore targets(c,state);ResourceStore resources(c);ImmediateRenderer immediate(c);
  Require(resources.BeginSubmission(fixture.command,1,e),e);Require(resources.CreateDummies(e),e);Require(targets.BeginSubmission(fixture.command,1,e),e);Require(immediate.Initialize(e),e);
  guest::LinearTexture texture;texture.width=texture.height=1;texture.format=guest::LinearFormat::kRGBA8Unorm;texture.levels={{1,1,4,1,0}};texture.data={255,255,255,255};Require(resources.UploadTexture(2002,texture,1,e),e);
  guest::SurfaceDesc target{2001,false,{16,16,0,0,0,1}};guest::ClearPacket clear{};clear.flags=1;clear.colors[0]=target;clear.color={0,0,1,1};Require(targets.Clear(clear,e),e);
  guest::DrawPacket draw;draw.colors[0]=target;auto pass=targets.PreparePass(PlanAttachments(draw,e),e);Require(bool(pass),e);
  std::array<UIVertex,4> vertices{{{0,0,0,0,0x800000ff},{16,0,1,0,0x800000ff},{16,16,1,1,0x800000ff},{0,16,0,1,0x800000ff}}};std::array<uint16_t,6> indices{0,1,2,0,2,3};
  VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass->render_pass;begin.framebuffer=pass->framebuffer;begin.renderArea={{0,0},pass->extent};c.f.vkCmdBeginRenderPass(fixture.command,&begin,VK_SUBPASS_CONTENTS_INLINE);
  Require(immediate.SetBatch(resources,vertices,indices,e),e);UIDraw ui;ui.count=6;ui.texture=2002;ui.scissor={{4,4},{8,8}};Require(immediate.Draw(fixture.command,*pass,resources,16,16,ui,e),e);c.f.vkCmdEndRenderPass(fixture.command);
  auto output=targets.Get(targets.Acquire(target,e),e);Require(state.Transition(fixture.command,output->image,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},ImageUsage::TransferSource(),e),e);
  VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={16,16,1};c.f.vkCmdCopyImageToBuffer(fixture.command,output->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,1,&copy);auto bytes=fixture.Finish();immediate.Retire(1);resources.Retire(1);targets.Retire(1);
  for(uint32_t y=0;y<16;++y) for(uint32_t x=0;x<16;++x) {bool inside=x>=4&&x<12&&y>=4&&y<12;uint8_t expected[]{uint8_t(inside?128:0),0,uint8_t(inside?127:255),255};if(std::memcmp(bytes.data()+(y*16+x)*4,expected,4)) throw std::runtime_error("Immediate UI texture/alpha/scissor pixels mismatch");}
  std::cout<<"Immediate host UI indexed triangles, texture, alpha blending and scissor pixels passed\n";
}
void CheckStackedResolve(Context& c) {
  Error e;UploadFixture fixture(c);ImageState state(c.f);ResourceStore resources(c);TargetStore targets(c,state);
  Require(resources.BeginSubmission(fixture.command,1,e),e);Require(targets.BeginSubmission(fixture.command,1,e),e);
  guest::SurfaceDesc source{3001,false,{16,16,0,0,0,1}};guest::ClearPacket clear{};clear.flags=1;clear.colors[0]=source;clear.color={1,0,0,1};Require(targets.Clear(clear,e),e);
  guest::ResolvePacket packet{};packet.source_surface=source;packet.destination=3002;packet.slice=3;packet.destination_fetch={2,0x100006|(1u<<10),15u|(15u<<13)|(3u<<26),0,0,1u<<9};
  auto plan=PlanResolve(packet,targets,e);Require(e.message.empty(),e);Require(RecordColorResolve(c,fixture.command,plan,targets,resources,state,e),e);auto image=resources.Texture(3002,e);Require(bool(image),e);
  Require(state.Transition(fixture.command,image->handle,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,4},ImageUsage::TransferSource(),e),e);
  std::array<VkBufferImageCopy,4> copies{};for(uint32_t layer=0;layer<4;++layer) {copies[layer].bufferOffset=layer*1024;copies[layer].imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,layer,1};copies[layer].imageExtent={16,16,1};}
  c.f.vkCmdCopyImageToBuffer(fixture.command,image->handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,fixture.readback,4,copies.data());auto bytes=fixture.Finish();resources.Retire(1);targets.Retire(1);
  for(uint32_t layer=0;layer<4;++layer) for(uint32_t pixel=0;pixel<256;++pixel) {uint8_t expected[]{uint8_t(layer==3?255:0),0,0,uint8_t(layer==3?255:0)};if(std::memcmp(bytes.data()+layer*1024+pixel*4,expected,4)) throw std::runtime_error("Stacked resolve changed an untouched slice");}
  std::cout<<"Stacked resolve preserves four layers with a valid single-layer 2D fetch view\n";
}
void CheckPipelineCheckpoint(Context& c) {
  const auto path=std::filesystem::temp_directory_path()/(
    "sr-vulkan-cache-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);std::filesystem::remove(path.string()+".tmp",ec);}} cleanup{path};
  Error e;DescriptorStore descriptors(c);Require(descriptors.Initialize(e),e);
  GamePipelineStore pipelines(c);Require(pipelines.Initialize(descriptors.Layouts(),path,e),e);
  shaders::CompiledShader vertex,pixel;pixel.stage=shaders::ShaderStage::kPixel;
  Require(ReadSpirv(std::filesystem::path(SR_TARGET_SHADER_DIR)/"vs.spv",vertex.words,e),e);
  Require(ReadSpirv(std::filesystem::path(SR_TARGET_SHADER_DIR)/"ps.spv",pixel.words,e),e);
  guest::DrawPacket draw;draw.registers[0x104]=15;draw.registers[0x201]=1|(1<<16);
  UploadFixture fixture(c);ImageState state(c.f);TargetStore targets(c,state);Require(targets.BeginSubmission(fixture.command,1,e),e);
  draw.colors[0]={4001,false,{16,16,0,0,0,1}};auto plan=PlanAttachments(draw,e);Require(e.message.empty(),e);auto pass=targets.PreparePass(plan,e);Require(bool(pass),e);
  Require(bool(pipelines.Acquire(draw,*pass,vertex,&pixel,1,e)),e);
  Require(pipelines.CheckpointCache(e),e);
  if(!std::filesystem::exists(path) || std::filesystem::file_size(path)<68) throw std::runtime_error("Driver cache not persisted before pipeline store destruction");
  draw.registers[0x201]=0;Require(bool(pipelines.Acquire(draw,*pass,vertex,&pixel,1,e)),e);Require(pipelines.CheckpointCache(e),e);
  if(std::filesystem::exists(path.string()+".tmp")) throw std::runtime_error("Driver cache temporary file was not replaced");
  GamePipelineStore reopened(c);Require(reopened.Initialize(descriptors.Layouts(),path,e),e);Require(bool(reopened.Acquire(draw,*pass,vertex,&pixel,1,e)),e);
  fixture.Finish();pipelines.Retire(1);reopened.Retire(1);targets.Retire(1);
  std::cout<<"Driver pipeline cache checkpoints before shutdown, replaces and reloads successfully\n";
}
// A texture replaced while a recorded draw still refers to it must live until its submission retires, on the
// cache-miss path and on the cache-hit paths (a hit in the same submission, and a hit from an earlier submission,
// which promotes the entry's resources). Every texture is uploaded in a submission that has already retired, so the
// upload's own Hold cannot be what keeps it alive: only the draw's Hold can.
// Nothing is submitted: the command buffer only exists so that the stores have a recording submission.
void CheckHoldLifetime(Context& c) {
  Error e;UploadFixture fixture(c);ResourceStore resources(c);DescriptorStore descriptors(c);Require(descriptors.Initialize(e),e);
  guest::DrawPacket packet;packet.texture_fetch[3][0]=2|(2<<10)|(2<<13)|(2<<16);packet.texture_fetch[3][1]=0x1006;packet.texture_fetch[3][5]=1<<9;
  const auto id=TextureResourceId(packet.texture_fetch[3]);
  auto begin=[&](uint64_t serial) {Require(resources.BeginSubmission(fixture.command,serial,e),e);if(serial==1) Require(resources.CreateDummies(e),e);};
  auto upload=[&](uint64_t version,uint8_t value) {
    guest::LinearTexture t;t.width=t.height=1;t.format=guest::LinearFormat::kRGBA8Unorm;t.levels={{1,1,4,1,0}};t.data={value,value,value,255};
    Require(resources.UploadTexture(id,t,version,e),e);
  };
  auto draw=[&](uint64_t serial) {
    TransientSlice constants;Require(resources.MapTransient(sizeof(guest::ConstantSnapshot),constants,e),e);
    DrawBindings bindings;Require(BuildBindings(packet,constants.data,bindings,e),e);
    DescriptorDraw sets;Require(descriptors.Prepare(bindings,packet.texture_fetch,resources,serial,constants,sets,e),e);
  };
  auto current=[&]() {return std::weak_ptr<TextureResource>(*resources.FindTexture(id));};
  auto expect=[](bool ok,const char* what) {if(!ok) throw std::runtime_error(what);};
  auto retire=[&](uint64_t serial) {
    // This fixture never submits. Discard recorded references before releasing
    // their resources, then start an empty command buffer for the next serial.
    Require(c.f.vkResetCommandPool(c.device,fixture.pool,0),"Discard simulated submission");
    descriptors.Retire(serial);resources.Retire(serial);
    VkCommandBufferBeginInfo next{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    next.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Require(c.f.vkBeginCommandBuffer(fixture.command,&next),"Begin simulated submission");
  };
  // Submission 1 only uploads v1 and retires: from now on v1 is held by the store alone.
  begin(1);upload(1,10);retire(1);
  // Submission 2, cache miss: the draw binds v1, then v1 is replaced by v2 before the fence.
  begin(2);auto v1=current();draw(2);
  expect(descriptors.TakeCacheStats().misses==1,"first draw must miss the descriptor cache");
  upload(2,20);expect(!v1.expired(),"a texture bound by a recorded draw was destroyed before its submission retired (miss path)");
  retire(2);expect(v1.expired(),"a replaced texture leaked after its submission retired");
  // Submission 3: a miss for v2, then the same bindings again in the same submission (a hit that skips the promotion).
  begin(3);auto v2=current();draw(3);draw(3);
  auto stats=descriptors.TakeCacheStats();expect(stats.misses==1 && stats.hits==1,"the second draw of a submission must hit the cache");
  upload(3,30);expect(!v2.expired(),"a texture bound by a recorded draw was destroyed before its submission retired (same-submission hit)");
  retire(3);expect(v2.expired(),"a replaced texture leaked after its submission retired");
  // Submission 4 builds the entry for v3 (a miss); submission 5 hits it (a hit from an earlier submission), then v3 is replaced.
  begin(4);draw(4);retire(4);descriptors.TakeCacheStats();
  begin(5);auto v3=current();draw(5);
  stats=descriptors.TakeCacheStats();expect(stats.hits==1 && stats.misses==0,"a later submission must hit the cached entry");
  upload(4,40);expect(!v3.expired(),"a texture promoted by a cache hit was destroyed before its submission retired (hit path)");
  retire(5);expect(v3.expired(),"a replaced texture leaked after its submission retired");
  std::cout<<"Resources bound by recorded draws stay alive until their submission retires (cache miss, hit in the same and in a later submission)\n";
}
// Repeated draws without textures still bind the 2D/3D/cube dummy images.
// UINT64_MAX is a valid dummy texture ID and can also be a buffer ID; neither
// may be treated as an unvisited lookup sentinel. Exercise the production store.
void CheckDescriptorCacheIds(Context& c) {
  Error e;UploadFixture fixture(c);ResourceStore resources(c);DescriptorStore descriptors(c);
  Require(resources.BeginSubmission(fixture.command,1,e),e);Require(resources.CreateDummies(e),e);
  Require(descriptors.Initialize(e),e);std::array<std::byte,16> bytes{};
  Require(resources.UploadHostBuffer(UINT64_MAX,bytes,1,e),e);
  guest::DrawPacket packet;TransientSlice constants;DrawBindings bindings;
  Require(resources.MapTransient(sizeof(guest::ConstantSnapshot),constants,e),e);
  Require(BuildBindings(packet,constants.data,bindings,e),e);
  for(bool full_width_buffer:{false,true}) {
    if(full_width_buffer) bindings.vertex_buffers[0]=UINT64_MAX;
    DescriptorDraw first,next;
    Require(descriptors.Prepare(bindings,packet.texture_fetch,resources,1,constants,first,e),e);
    Require(descriptors.Prepare(bindings,packet.texture_fetch,resources,1,constants,next,e),e);
    const auto stats=descriptors.TakeCacheStats();
    if(stats.misses!=1 || stats.hits!=1 || first.sets!=next.sets)
      throw std::runtime_error("full-width resource ID skipped lookup: identical dummy bindings must cache");
  }
  // Keeping a cached entry alive must not make a removed binding valid.
  resources.ForgetTexture(DummyTexture(TextureDimension::k2D));DescriptorDraw next;
  if(descriptors.Prepare(bindings,packet.texture_fetch,resources,1,constants,next,e))
    throw std::runtime_error("missing dummy texture incorrectly reused cached descriptors");
  fixture.Finish();descriptors.Retire(1);resources.Retire(1);
  std::cout<<"Descriptor cache accepts UINT64_MAX texture/buffer IDs and rejects removed bindings\n";
}
void BenchmarkDummyDescriptors(Context& c) {
  Error e;UploadFixture fixture(c);ResourceStore resources(c);DescriptorStore descriptors(c);
  Require(resources.BeginSubmission(fixture.command,1,e),e);Require(resources.CreateDummies(e),e);
  Require(descriptors.Initialize(e),e);guest::DrawPacket packet;TransientSlice constants;DrawBindings bindings;
  Require(resources.MapTransient(sizeof(guest::ConstantSnapshot),constants,e),e);
  Require(BuildBindings(packet,constants.data,bindings,e),e);DescriptorDraw out;
  constexpr uint32_t draws=3000;const auto start=std::chrono::steady_clock::now();
  for(uint32_t i=0;i<draws;++i) Require(descriptors.Prepare(bindings,packet.texture_fetch,resources,1,constants,out,e),e);
  const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/draws;
  const auto stats=descriptors.TakeCacheStats();
  std::cout<<"dummy_descriptor_bench draws="<<draws<<" us/draw="<<us<<" hits="<<stats.hits<<" misses="<<stats.misses<<'\n';
  fixture.Finish();descriptors.Retire(1);resources.Retire(1);
}
int main(int argc,char** argv) {
  try {
    Win32Window window;Context c;Error e;
    c.logger=[](const std::string& s){std::cout<<s<<'\n';};
    const char* extensions[]{VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    Require(c.CreateInstance(extensions,true,e),e);Require(window.Open(64,64,e,false),e);
    auto surface=window.CreateSurface(c,e);Require(bool(surface),e);Require(c.OpenDevice(surface,"",e),e);
    if(argc==2 && std::string(argv[1])=="--texture-updates") {CheckTextureUpdates(c);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--texture-updates-separated") {CheckTextureUpdates(c,true);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--targets") {CheckTargetClears(c);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--pipeline-cache") {CheckPipelineCheckpoint(c);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--target-draws") {CheckTargetDraws(c);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--game-record") {CheckTargetDraws(c,true);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--game-record-merged") {CheckTargetDraws(c,true,true);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--resolve-record") {CheckResolveCopy(c,true);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--depth-resolve") {CheckDepthResolve(c);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--edram-alias") {CheckEdramAlias(c);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--alias-defaults") {CheckEdramAlias(c,true);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--resolve-sample") {CheckResolvedDraw(c);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--texture-mask") {CheckResolvedDraw(c,true);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--constant-mask") {CheckConstantMask(c);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--texture-storage") {CheckTextureStorageReuse(c);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--texture-storage-merged") {CheckTextureStorageReuse(c,false,true);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--game-frame") {CheckGameFrame(c);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--game-frame-async") {CheckGameFrameAsync(c);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--immediate") {CheckImmediate(c);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--stacked-resolve") {CheckStackedResolve(c);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--composition") {CheckComposition(c);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--bench-bindings") {BenchmarkMappedBindings(c);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--descriptor-cache-ids") {CheckDescriptorCacheIds(c);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--bench-descriptor-dummies") {BenchmarkDummyDescriptors(c);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--hold-lifetime") {CheckHoldLifetime(c);return c.validation_errors.load()?1:0;}
    if(argc==2 && std::string(argv[1])=="--resolve-copy") {CheckResolveCopy(c);return c.validation_errors.load()?1:0;}
    if(argc!=1) throw std::runtime_error("Unknown GPU fixture argument; refusing a different test");
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
