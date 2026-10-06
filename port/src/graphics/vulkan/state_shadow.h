#pragma once
#include "loader.h"
#include <array>
#include <cstring>
namespace superman_returns::graphics::vulkan {
// What the recorder last emitted on the open command buffer. A command is skipped only when the same value is
// already in effect. Invalidate() forgets everything; call it wherever the command buffer's state can change
// behind the recorder's back (new submission, end of a render pass, any other recorder). With `enabled=false`
// every Set* asks for the command (SR_VULKAN_NO_STATE_FILTER=1: A/B diagnostics).
class StateShadow {
public:
  explicit StateShadow(bool enabled=true):enabled_(enabled) {}
  bool enabled() const {return enabled_;}
  void Invalidate() {*this=StateShadow(enabled_);}
  bool SetViewport(const VkViewport& value) {return Update(viewport_,value);}
  bool SetScissor(const VkRect2D& value) {return Update(scissor_,value);}
  bool SetBlend(const float (&value)[4]) {return Update(blend_,std::array<float,4>{value[0],value[1],value[2],value[3]});}
  bool SetStencil(uint32_t reference) {return Update(stencil_,reference);}
  // Descriptor sets 1-3 (textures, samplers, vertex buffers), as one group of three handles.
  bool SetSharedSets(const VkDescriptorSet* sets) {return Update(sets_,std::array<VkDescriptorSet,3>{sets[0],sets[1],sets[2]});}
  bool SetVertexBuffer(uint32_t slot,VkBuffer buffer,VkDeviceSize offset) {
    if(slot>=vertex_.size()) return true;  // not tracked: always emit
    auto& v=vertex_[slot];
    if(enabled_ && v.valid && v.buffer==buffer && v.offset==offset) return false;
    v={buffer,offset,true};return true;
  }
  bool SetIndexBuffer(VkBuffer buffer,VkDeviceSize offset,VkIndexType type) {
    if(enabled_ && index_.valid && index_.buffer==buffer && index_.offset==offset && index_.type==type) return false;
    index_={buffer,offset,type,true};return true;
  }
private:
  // Compared bitwise: the types used here have no padding, and -0.0f must not equal +0.0f.
  template<class T> struct Slot {T value{};bool valid=false;};
  template<class T> bool Update(Slot<T>& slot,const T& value) {
    if(enabled_ && slot.valid && std::memcmp(&slot.value,&value,sizeof(T))==0) return false;
    slot.value=value;slot.valid=true;return true;
  }
  struct VertexBinding {VkBuffer buffer=VK_NULL_HANDLE;VkDeviceSize offset=0;bool valid=false;};
  struct IndexBinding {VkBuffer buffer=VK_NULL_HANDLE;VkDeviceSize offset=0;VkIndexType type=VK_INDEX_TYPE_UINT16;bool valid=false;};
  bool enabled_;
  Slot<VkViewport> viewport_;Slot<VkRect2D> scissor_;Slot<std::array<float,4>> blend_;Slot<uint32_t> stencil_;
  Slot<std::array<VkDescriptorSet,3>> sets_;
  std::array<VertexBinding,32> vertex_{};
  IndexBinding index_;
};
}
