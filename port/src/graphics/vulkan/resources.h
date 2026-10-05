#pragma once
#include "context.h"
#include "../guest/texture_layout.h"
#include "../guest/render_packet.h"
#include <atomic>
#include <map>
#include <memory>
namespace superman_returns::graphics::vulkan {
class ImageState;
class SubmissionResources {
public:
  void Keep(uint64_t serial,std::shared_ptr<void> resource);
  void Retire(uint64_t completed_serial);
private:
  std::map<uint64_t,std::vector<std::shared_ptr<void>>> pending_;
};
struct TextureUploadPlan {
  VkFormat format=VK_FORMAT_UNDEFINED;
  VkImageType image_type=VK_IMAGE_TYPE_2D;
  VkImageViewType view_type=VK_IMAGE_VIEW_TYPE_2D;
  VkImageCreateFlags flags=0;
  VkExtent3D extent{};
  uint32_t layers=1,mips=1;
  VkComponentMapping swizzle{};
  std::vector<VkBufferImageCopy> regions;
  std::vector<uint8_t> bytes;
};
bool PlanTextureUpload(const guest::LinearTexture&,VkDeviceSize alignment,TextureUploadPlan&,Error&);
// Live VkDeviceMemory owned by buffers/textures of this module (diagnostics).
struct MemoryCounters {std::atomic<int64_t> buffers{0},buffer_bytes{0},images{0},image_bytes{0};};
MemoryCounters& LiveMemory();
struct BufferResource {
  Context* context=nullptr;
  // A suballocation shares owner's handle; bind it at offset, never destroy it.
  std::shared_ptr<BufferResource> owner;
  VkBuffer handle=VK_NULL_HANDLE;
  VkDeviceMemory memory=VK_NULL_HANDLE;
  VkDeviceSize size=0,allocation=0,offset=0;
  uint64_t version=0;
  bool coherent=false;
  void* mapped=nullptr;  // persistent mapping of host-visible allocations
  ~BufferResource();
};
struct TextureResource {
  Context* context=nullptr;
  ImageState* state=nullptr;
  std::shared_ptr<TextureResource> image_owner;
  VkImage handle=VK_NULL_HANDLE;
  VkDeviceMemory memory=VK_NULL_HANDLE;
  VkImageView view=VK_NULL_HANDLE;
  VkFormat format=VK_FORMAT_UNDEFINED;
  VkExtent3D extent{};
  uint32_t layers=1,mips=1;
  VkImageViewType view_type=VK_IMAGE_VIEW_TYPE_2D;
  uint64_t version=0;VkDeviceSize allocation=0;
  ~TextureResource();
};
// Context must outlive the store and any resources retained by descriptor sets.
// All methods run on the render worker. BeginSubmission requires a recording
// command buffer; replacement is immutable and never destroys pending versions.
class ResourceStore {
public:
  explicit ResourceStore(Context& c):c_(c) {}
  ~ResourceStore();
  // With a separate upload command buffer (submitted before the main one),
  // buffer/texture uploads record there and share one barrier written by
  // FinishUploads. Every upload creates a new version, so hoisting is safe.
  bool BeginSubmission(VkCommandBuffer,uint64_t serial,Error&,VkCommandBuffer upload=VK_NULL_HANDLE);
  void FinishUploads();
  void Retire(uint64_t completed_serial);
  struct TransientBuffer { VkBuffer handle; VkDeviceSize offset; explicit operator bool() const { return handle != VK_NULL_HANDLE; } };
  struct MappedTransient { VkBuffer handle; VkDeviceSize offset; std::byte* mapped; explicit operator bool() const { return handle != VK_NULL_HANDLE; } };
  // Host-visible per-submission suballocation; the returned view binds at
  // its offset and stays valid until this submission retires.
  TransientBuffer UploadTransient(guest::ResourceId,std::span<const std::byte>,VkDeviceSize reserve,Error&);
  MappedTransient MapTransient(VkDeviceSize size,Error& e);
  bool UploadBuffer(guest::ResourceId,std::span<const std::byte>,uint64_t version,Error&);
  bool UploadTexture(guest::ResourceId,const guest::LinearTexture&,uint64_t version,Error&);
  // Tracked resolve images remain GPU-authoritative and preserve partial writes.
  // The supplied ImageState must outlive this store and descriptor leases.
  std::shared_ptr<TextureResource> ResolveTexture(guest::ResourceId,VkFormat,VkExtent3D,uint32_t mips,uint32_t layers,bool cube,ImageState&,Error&);
  std::shared_ptr<BufferResource> ResolveScratch(VkDeviceSize,Error&);
  std::shared_ptr<BufferResource> ReadbackBuffer(VkDeviceSize,Error&);
  bool Readback(const std::shared_ptr<BufferResource>&,std::vector<uint8_t>&,Error&);
  bool BindTextureView(guest::ResourceId,std::shared_ptr<TextureResource>,VkComponentMapping,Error&);
  std::shared_ptr<BufferResource> Buffer(guest::ResourceId,Error&);
  // Lookups without registering the resource with the current submission;
  // the caller must keep what it binds alive (descriptor cache entries do).
  const std::shared_ptr<BufferResource>* FindBuffer(guest::ResourceId id) const {auto f=buffers_.find(id);return f==buffers_.end()?nullptr:&f->second;}
  const std::shared_ptr<TextureResource>* FindTexture(guest::ResourceId id) const {auto f=textures_.find(id);return f==textures_.end()?nullptr:&f->second;}
  std::shared_ptr<TextureResource> Texture(guest::ResourceId,Error&);
  bool CreateDummies(Error&);
  bool UploadHostBuffer(guest::ResourceId,std::span<const std::byte>,uint64_t version,Error&);
  uint64_t CurrentSerial() const {return serial_;}
  // Bytes staged for buffer/texture uploads in the current submission.
  VkDeviceSize SubmissionBytes() const {return submission_bytes_;}
  void ForgetBuffer(guest::ResourceId id) {buffers_.erase(id);}
  void ForgetTexture(guest::ResourceId id) {textures_.erase(id);}
private:
  std::shared_ptr<BufferResource> NewBuffer(VkDeviceSize,VkBufferUsageFlags,VkMemoryPropertyFlags,Error&);
  bool Write(const std::shared_ptr<BufferResource>&,std::span<const std::byte>,Error&,VkDeviceSize offset=0);
  bool Ready(Error&);
  struct ArenaChunk {std::shared_ptr<BufferResource> buffer;VkDeviceSize used=0;uint64_t serial=0;};
  // `overflow` is the current chunk once the pooled budget is spent; it is
  // filled like a pooled chunk and released when its submission retires.
  struct Arena {VkBufferUsageFlags usage=0;std::vector<ArenaChunk> chunks;size_t current=SIZE_MAX;VkDeviceSize bytes=0;ArenaChunk overflow;};
  std::shared_ptr<BufferResource> Suballocate(Arena&,VkDeviceSize size,VkDeviceSize alignment,Error&);
  Context& c_;
  VkCommandBuffer command_=VK_NULL_HANDLE,upload_=VK_NULL_HANDLE;
  bool upload_barrier_=false;std::vector<VkImageMemoryBarrier> texture_barriers_;VkDeviceSize submission_bytes_=0;
  Arena staging_{VK_BUFFER_USAGE_TRANSFER_SRC_BIT},transient_{VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT};
  uint64_t serial_=0,completed_=0;
  SubmissionResources submissions_;
  std::map<guest::ResourceId,std::shared_ptr<BufferResource>> buffers_;
  struct HostPool {std::vector<std::shared_ptr<BufferResource>> buffers;size_t cursor=0;};
  std::map<VkDeviceSize,HostPool> host_pool_;
  VkDeviceSize host_pool_bytes_=0;
  std::map<guest::ResourceId,std::shared_ptr<TextureResource>> textures_;
};
} // namespace superman_returns::graphics::vulkan
