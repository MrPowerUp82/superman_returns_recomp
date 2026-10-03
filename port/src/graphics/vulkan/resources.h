#pragma once
#include "context.h"
#include "../guest/texture_layout.h"
#include "../guest/render_packet.h"
#include <map>
#include <memory>
namespace superman_returns::graphics::vulkan {
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
struct BufferResource {
  Context* context=nullptr;
  VkBuffer handle=VK_NULL_HANDLE;
  VkDeviceMemory memory=VK_NULL_HANDLE;
  VkDeviceSize size=0,allocation=0;
  uint64_t version=0;
  bool coherent=false;
  ~BufferResource();
};
struct TextureResource {
  Context* context=nullptr;
  VkImage handle=VK_NULL_HANDLE;
  VkDeviceMemory memory=VK_NULL_HANDLE;
  VkImageView view=VK_NULL_HANDLE;
  VkFormat format=VK_FORMAT_UNDEFINED;
  VkExtent3D extent{};
  uint32_t layers=1,mips=1;
  VkImageViewType view_type=VK_IMAGE_VIEW_TYPE_2D;
  uint64_t version=0;
  ~TextureResource();
};
// Context must outlive the store and any resources retained by descriptor sets.
// All methods run on the render worker. BeginSubmission requires a recording
// command buffer; replacement is immutable and never destroys pending versions.
class ResourceStore {
public:
  explicit ResourceStore(Context& c):c_(c) {}
  ~ResourceStore();
  bool BeginSubmission(VkCommandBuffer,uint64_t serial,Error&);
  void Retire(uint64_t completed_serial);
  bool UploadBuffer(guest::ResourceId,std::span<const std::byte>,uint64_t version,Error&);
  bool UploadTexture(guest::ResourceId,const guest::LinearTexture&,uint64_t version,Error&);
  std::shared_ptr<BufferResource> Buffer(guest::ResourceId,Error&);
  std::shared_ptr<TextureResource> Texture(guest::ResourceId,Error&);
  bool CreateDummies(Error&);
  bool UploadHostBuffer(guest::ResourceId,std::span<const std::byte>,uint64_t version,Error&);
  uint64_t CurrentSerial() const {return serial_;}
  void ForgetBuffer(guest::ResourceId id) {buffers_.erase(id);}
private:
  std::shared_ptr<BufferResource> NewBuffer(VkDeviceSize,VkBufferUsageFlags,VkMemoryPropertyFlags,Error&);
  bool Write(const std::shared_ptr<BufferResource>&,std::span<const std::byte>,Error&);
  bool Ready(Error&);
  Context& c_;
  VkCommandBuffer command_=VK_NULL_HANDLE;
  uint64_t serial_=0,completed_=0;
  SubmissionResources submissions_;
  std::map<guest::ResourceId,std::shared_ptr<BufferResource>> buffers_;
  std::map<guest::ResourceId,std::shared_ptr<TextureResource>> textures_;
};
} // namespace superman_returns::graphics::vulkan
