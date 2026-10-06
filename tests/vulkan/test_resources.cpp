#include "resources.h"
#include "test_main.h"
#include <cstring>
using namespace superman_returns::graphics::vulkan;
SR_TEST(submission_resources_retire_only_completed_versions) {
  SubmissionResources submissions;
  auto old=std::make_shared<int>(1),current=std::make_shared<int>(2);
  std::weak_ptr<int> old_view=old,current_view=current;
  submissions.Keep(4,old);submissions.Keep(7,current);
  old.reset();current.reset();
  submissions.Retire(3);SR_CHECK(!old_view.expired());
  submissions.Retire(4);SR_CHECK(old_view.expired());SR_CHECK(!current_view.expired());
  submissions.Retire(7);SR_CHECK(current_view.expired());
}
SR_TEST(submission_resources_hold_keeps_a_resource_once_per_serial_and_until_retire) {
  SubmissionResources submissions;
  auto buffer=std::make_shared<BufferResource>();std::weak_ptr<BufferResource> view=buffer;
  submissions.Hold(4,buffer);submissions.Hold(4,buffer);submissions.Hold(4,buffer);
  SR_CHECK_EQ(buffer.use_count(),2);          // the caller's reference and one entry of submission 4
  submissions.Hold(7,buffer);
  SR_CHECK_EQ(buffer.use_count(),3);          // a later submission holds it again
  buffer.reset();                              // the store replaced the resource: only the lists keep it
  submissions.Retire(4);SR_CHECK(!view.expired());
  submissions.Retire(7);SR_CHECK(view.expired());
  // Lists are reused: later submissions still work and release their resources.
  auto next=std::make_shared<BufferResource>();std::weak_ptr<BufferResource> next_view=next;
  submissions.Hold(9,next);next.reset();SR_CHECK(!next_view.expired());
  submissions.Retire(9);SR_CHECK(next_view.expired());
}
SR_TEST(texture_upload_plan_keeps_cube_faces_and_volume_slices) {
  superman_returns::graphics::guest::LinearTexture t;
  t.width=2;t.height=2;t.depth=6;t.dimension=3;t.format=superman_returns::graphics::guest::LinearFormat::kRGBA8Unorm;
  for(uint32_t face=0;face<6;++face) {t.levels.push_back({2,2,8,2,face*16u});t.data.insert(t.data.end(),16,uint8_t(face));}
  TextureUploadPlan p;Error e;
  SR_CHECK(PlanTextureUpload(t,4,p,e));
  SR_CHECK_EQ(p.layers,6u);SR_CHECK_EQ(p.extent.depth,1u);SR_CHECK_EQ(p.regions.size(),6u);
  SR_CHECK(p.view_type==VK_IMAGE_VIEW_TYPE_CUBE);
  for(uint32_t face=0;face<6;++face) {SR_CHECK_EQ(p.regions[face].imageSubresource.baseArrayLayer,face);SR_CHECK_EQ(p.bytes[p.regions[face].bufferOffset],face);}
  t.dimension=2;
  SR_CHECK(PlanTextureUpload(t,256,p,e));
  SR_CHECK_EQ(p.layers,1u);SR_CHECK_EQ(p.extent.depth,6u);
  for(uint32_t z=0;z<6;++z) {SR_CHECK_EQ(p.regions[z].imageOffset.z,z);SR_CHECK_EQ(p.regions[z].bufferOffset%256,0u);}
  t.levels[0].row_pitch=1;
  SR_CHECK(!PlanTextureUpload(t,4,p,e));
}
namespace {
uint64_t next_handle=10;
std::map<VkDeviceMemory,std::vector<uint8_t>> allocations;
int copies=0,flushes=0,destroyed=0;
VkResult VKAPI_CALL FakeBuffer(VkDevice,const VkBufferCreateInfo*,const VkAllocationCallbacks*,VkBuffer* out) {
  *out=reinterpret_cast<VkBuffer>(++next_handle);return VK_SUCCESS;
}
void VKAPI_CALL FakeRequirements(VkDevice,VkBuffer,VkMemoryRequirements* out) {*out={64,4,3};}
VkResult VKAPI_CALL FakeAllocate(VkDevice,const VkMemoryAllocateInfo* info,const VkAllocationCallbacks*,VkDeviceMemory* out) {
  *out=reinterpret_cast<VkDeviceMemory>(++next_handle);allocations[*out].resize(info->allocationSize);return VK_SUCCESS;
}
VkResult VKAPI_CALL FakeBind(VkDevice,VkBuffer,VkDeviceMemory,VkDeviceSize) {return VK_SUCCESS;}
VkResult VKAPI_CALL FakeMap(VkDevice,VkDeviceMemory memory,VkDeviceSize,VkDeviceSize,VkMemoryMapFlags,void** out) {*out=allocations.at(memory).data();return VK_SUCCESS;}
void VKAPI_CALL FakeUnmap(VkDevice,VkDeviceMemory) {}
VkResult VKAPI_CALL FakeFlush(VkDevice,uint32_t n,const VkMappedMemoryRange* ranges) {
  SR_CHECK_EQ(n,1u);SR_CHECK_EQ(ranges[0].offset,0u);SR_CHECK_EQ(ranges[0].size,64u);++flushes;return VK_SUCCESS;
}
void VKAPI_CALL FakeFree(VkDevice,VkDeviceMemory memory,const VkAllocationCallbacks*) {allocations.erase(memory);}
void VKAPI_CALL FakeDestroy(VkDevice,VkBuffer,const VkAllocationCallbacks*) {++destroyed;}
void VKAPI_CALL FakeCopy(VkCommandBuffer,VkBuffer,VkBuffer,uint32_t n,const VkBufferCopy* copy) {SR_CHECK_EQ(n,1u);SR_CHECK_EQ(copy->size,4u);++copies;}
void VKAPI_CALL FakeBarrier(VkCommandBuffer,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags,uint32_t,const VkMemoryBarrier*,uint32_t n,const VkBufferMemoryBarrier* barriers,uint32_t,const VkImageMemoryBarrier*) {
  SR_CHECK(src&VK_PIPELINE_STAGE_TRANSFER_BIT);SR_CHECK(dst&VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);SR_CHECK_EQ(n,1u);SR_CHECK(barriers[0].dstAccessMask&VK_ACCESS_SHADER_READ_BIT);
}
VkResult VKAPI_CALL FakeIdle(VkDevice) {return VK_SUCCESS;}
void VKAPI_CALL FakeDeviceDestroy(VkDevice,const VkAllocationCallbacks*) {}
Dispatch ResourceFake() {
  Dispatch f;f.vkCreateBuffer=FakeBuffer;f.vkGetBufferMemoryRequirements=FakeRequirements;
  f.vkAllocateMemory=FakeAllocate;f.vkBindBufferMemory=FakeBind;f.vkMapMemory=FakeMap;
  f.vkUnmapMemory=FakeUnmap;f.vkFlushMappedMemoryRanges=FakeFlush;f.vkFreeMemory=FakeFree;
  f.vkDestroyBuffer=FakeDestroy;f.vkCmdCopyBuffer=FakeCopy;f.vkCmdPipelineBarrier=FakeBarrier;
  f.vkDeviceWaitIdle=FakeIdle;f.vkDestroyDevice=FakeDeviceDestroy;return f;
}
}
SR_TEST(buffer_upload_flushes_staging_and_retains_replaced_version) {
  copies=flushes=destroyed=0;allocations.clear();
  Context c(ResourceFake());c.device=reinterpret_cast<VkDevice>(1);
  c.memory.memoryTypeCount=2;c.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  c.memory.memoryTypes[1].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  c.properties.limits.maxStorageBufferRange=4096;c.properties.limits.nonCoherentAtomSize=64;
  std::weak_ptr<BufferResource> old;
  {
    ResourceStore store(c);Error e;std::array<std::byte,4> bytes{};
    SR_CHECK(!store.UploadBuffer(1500,bytes,1,e));
    SR_CHECK(store.BeginSubmission(reinterpret_cast<VkCommandBuffer>(1),4,e));
    SR_CHECK(store.UploadBuffer(1500,bytes,1,e));
    old=store.Buffer(1500,e);
    SR_CHECK(store.BeginSubmission(reinterpret_cast<VkCommandBuffer>(1),7,e));
    SR_CHECK(store.UploadBuffer(1500,bytes,2,e));
    SR_CHECK(!old.expired());SR_CHECK_EQ(copies,2);SR_CHECK_EQ(flushes,2);
    // Rebinding the same version records no extra upload.
    SR_CHECK(store.UploadBuffer(1500,bytes,2,e));SR_CHECK_EQ(copies,2);
    SR_CHECK(!store.UploadBuffer(1500,bytes,1,e));
    store.Retire(3);SR_CHECK(!old.expired());SR_CHECK_EQ(destroyed,0);
    // The replaced version is freed; its staging chunk stays pooled for reuse.
    store.Retire(4);SR_CHECK(old.expired());SR_CHECK_EQ(destroyed,1);
    store.Retire(7);const auto before=allocations.size();
    SR_CHECK(store.BeginSubmission(reinterpret_cast<VkCommandBuffer>(1),8,e));
    SR_CHECK(store.UploadBuffer(1500,bytes,3,e));SR_CHECK_EQ(copies,3);
    // v3 replaces the retired v2 and reuses a staging chunk: no net allocation.
    SR_CHECK_EQ(allocations.size(),before);
  }
  SR_CHECK(allocations.empty());SR_CHECK_EQ(destroyed,5);
}
SR_TEST(host_upload_reuses_only_completed_unreferenced_buffers) {
  allocations.clear();copies=flushes=destroyed=0;
  Context c(ResourceFake());c.device=reinterpret_cast<VkDevice>(1);
  c.memory.memoryTypeCount=1;c.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  c.properties.limits.maxStorageBufferRange=4096;c.properties.limits.nonCoherentAtomSize=64;
  {
    ResourceStore store(c);Error e;std::array<std::byte,4> a{std::byte{1}},b{std::byte{2}};
    SR_CHECK(store.BeginSubmission(reinterpret_cast<VkCommandBuffer>(1),1,e));
    SR_CHECK(store.UploadHostBuffer(100,a,1,e));auto old=store.Buffer(100,e);store.ForgetBuffer(100);
    const auto old_handle=old->handle;
    const auto old_memory=old->memory;
    SR_CHECK(store.UploadHostBuffer(101,b,1,e));auto pending=store.Buffer(101,e);store.ForgetBuffer(101);
    SR_CHECK(pending->handle!=old_handle);SR_CHECK_EQ(allocations[old_memory][0],1u);
    pending.reset();store.Retire(1);
    SR_CHECK(store.BeginSubmission(reinterpret_cast<VkCommandBuffer>(1),2,e));
    SR_CHECK(store.UploadHostBuffer(102,b,1,e));auto next=store.Buffer(102,e);store.ForgetBuffer(102);
    SR_CHECK(next->handle!=old_handle);SR_CHECK_EQ(allocations[old_memory][0],1u);
    next.reset();old.reset();store.Retire(2);
    SR_CHECK(store.BeginSubmission(reinterpret_cast<VkCommandBuffer>(1),3,e));
    SR_CHECK(store.UploadHostBuffer(103,b,1,e));auto reused=store.Buffer(103,e);
    SR_CHECK_EQ(reused->handle,old_handle);SR_CHECK_EQ(allocations[reused->memory][0],2u);
    SR_CHECK_EQ(allocations.size(),2u); // No allocations after the two initial leases.
  }
  SR_CHECK(allocations.empty());SR_CHECK_EQ(copies,0);SR_CHECK_EQ(flushes,4);
}
SR_TEST(map_transient_hands_out_mapped_slices_of_one_chunk_and_flushes_non_coherent_memory) {
  allocations.clear();copies=flushes=destroyed=0;
  Context c(ResourceFake());c.device=reinterpret_cast<VkDevice>(1);
  c.memory.memoryTypeCount=1;c.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;  // not coherent
  c.properties.limits.maxStorageBufferRange=4096;c.properties.limits.nonCoherentAtomSize=64;c.properties.limits.minStorageBufferOffsetAlignment=16;
  {
    ResourceStore store(c);Error e;TransientSlice a,b;
    SR_CHECK(!store.MapTransient(16,a,e));                       // no recording submission yet
    SR_CHECK(store.BeginSubmission(reinterpret_cast<VkCommandBuffer>(1),1,e));
    SR_CHECK(!store.MapTransient(0,a,e));SR_CHECK(!store.MapTransient(5000,a,e));  // empty and over maxStorageBufferRange
    SR_CHECK(store.MapTransient(16,a,e));SR_CHECK(store.MapTransient(16,b,e));
    SR_CHECK(a.data!=nullptr);SR_CHECK(a.chunk==b.chunk);
    SR_CHECK_EQ(a.offset,0u);SR_CHECK_EQ(b.offset,64u);          // aligned to the non-coherent atom size
    SR_CHECK_EQ(size_t(b.data-a.data),size_t(64));
    std::memset(a.data,0x5a,16);                                  // the slice is the mapped memory itself
    SR_CHECK_EQ(allocations[a.chunk->memory][0],0x5au);
    SR_CHECK(store.FlushTransient(a,e));SR_CHECK_EQ(flushes,1);   // FakeFlush checks offset 0, size 64
    store.Retire(1);
  }
  SR_CHECK(allocations.empty());
}
SR_TEST(map_transient_skips_the_flush_on_coherent_memory) {
  allocations.clear();copies=flushes=destroyed=0;
  Context c(ResourceFake());c.device=reinterpret_cast<VkDevice>(1);
  c.memory.memoryTypeCount=1;c.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  c.properties.limits.maxStorageBufferRange=4096;c.properties.limits.nonCoherentAtomSize=64;
  {
    ResourceStore store(c);Error e;TransientSlice a;
    SR_CHECK(store.BeginSubmission(reinterpret_cast<VkCommandBuffer>(1),1,e));
    SR_CHECK(store.MapTransient(16,a,e));SR_CHECK(store.FlushTransient(a,e));SR_CHECK_EQ(flushes,0);
    store.Retire(1);
  }
  SR_CHECK(allocations.empty());
}
SR_TEST(resource_lookups_survive_rehash_replace_and_forget_across_thousands_of_ids) {
  allocations.clear();copies=flushes=destroyed=0;
  Context c(ResourceFake());c.device=reinterpret_cast<VkDevice>(1);
  c.memory.memoryTypeCount=1;c.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  c.properties.limits.maxStorageBufferRange=4096;c.properties.limits.nonCoherentAtomSize=64;
  {
    ResourceStore store(c);Error e;std::array<std::byte,4> bytes{};constexpr uint64_t count=5000;  // more than the initial reserve: forces a rehash
    SR_CHECK(store.BeginSubmission(reinterpret_cast<VkCommandBuffer>(1),1,e));
    SR_CHECK(store.FindBuffer(1)==nullptr);
    SR_CHECK(store.UploadHostBuffer(1,bytes,1,e));
    const auto* first=store.FindBuffer(1);SR_CHECK(first!=nullptr);
    for(uint64_t id=2;id<=count;++id) SR_CHECK(store.UploadHostBuffer(id,bytes,id,e));
    // Pointers handed out earlier stay valid when the table grows.
    SR_CHECK(store.FindBuffer(1)==first);SR_CHECK_EQ((*first)->version,1u);
    for(uint64_t id=1;id<=count;++id) {auto* found=store.FindBuffer(id);SR_CHECK(found!=nullptr);if(found) SR_CHECK_EQ((*found)->version,id);}
    SR_CHECK(store.FindBuffer(count+1)==nullptr);SR_CHECK(store.FindBuffer(0)==nullptr);
    // Replacing an id stores the new version under the same key.
    for(uint64_t id=3;id<=count;id+=3) SR_CHECK(store.UploadHostBuffer(id,bytes,id+100000,e));
    for(uint64_t id=1;id<=count;++id) {auto* found=store.FindBuffer(id);SR_CHECK(found!=nullptr);if(found) SR_CHECK_EQ((*found)->version,id%3==0?id+100000:id);}
    // Forgetting removes exactly that id and nothing else.
    for(uint64_t id=1;id<=count;id+=2) store.ForgetBuffer(id);
    for(uint64_t id=1;id<=count;++id) {
      auto* found=store.FindBuffer(id);
      if(id%2) {SR_CHECK(found==nullptr);SR_CHECK(!store.Buffer(id,e));}
      else {SR_CHECK(found!=nullptr);auto held=store.Buffer(id,e);SR_CHECK(held!=nullptr);if(found && held) SR_CHECK(held==*found);}
    }
    store.Retire(1);
  }
  SR_CHECK(allocations.empty());
}
