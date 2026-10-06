#include "state_shadow.h"
#include "test_main.h"
using namespace superman_returns::graphics::vulkan;
namespace {
VkBuffer Buffer(uintptr_t value) {return reinterpret_cast<VkBuffer>(value);}
VkDescriptorSet Set(uintptr_t value) {return reinterpret_cast<VkDescriptorSet>(value);}
}
SR_TEST(state_shadow_skips_repeated_dynamic_state_and_reemits_changes) {
  StateShadow s;
  VkViewport viewport{0,0,1280,720,0,1};
  SR_CHECK(s.SetViewport(viewport));SR_CHECK(!s.SetViewport(viewport));
  viewport.width=640;SR_CHECK(s.SetViewport(viewport));SR_CHECK(!s.SetViewport(viewport));
  VkRect2D scissor{{1,2},{3,4}};
  SR_CHECK(s.SetScissor(scissor));SR_CHECK(!s.SetScissor(scissor));scissor.offset.x=0;SR_CHECK(s.SetScissor(scissor));
  float blend[4]{0,0.5f,1,0};
  SR_CHECK(s.SetBlend(blend));SR_CHECK(!s.SetBlend(blend));blend[2]=0.25f;SR_CHECK(s.SetBlend(blend));
  SR_CHECK(s.SetStencil(0));SR_CHECK(!s.SetStencil(0));SR_CHECK(s.SetStencil(255));SR_CHECK(!s.SetStencil(255));
}
SR_TEST(state_shadow_compares_blend_constants_bitwise) {
  StateShadow s;
  float positive[4]{0,0,0,0},negative[4]{-0.0f,0,0,0};
  SR_CHECK(s.SetBlend(positive));
  SR_CHECK(s.SetBlend(negative));   // -0 and +0 are different values on the GPU: emit
  SR_CHECK(!s.SetBlend(negative));
}
SR_TEST(state_shadow_tracks_the_three_shared_sets_as_a_group) {
  StateShadow s;
  VkDescriptorSet sets[3]{Set(0x10),Set(0x20),Set(0x30)};
  SR_CHECK(s.SetSharedSets(sets));SR_CHECK(!s.SetSharedSets(sets));
  sets[2]=Set(0x31);SR_CHECK(s.SetSharedSets(sets));SR_CHECK(!s.SetSharedSets(sets));
  sets[0]=Set(0x11);SR_CHECK(s.SetSharedSets(sets));
}
SR_TEST(state_shadow_tracks_each_vertex_slot_and_the_index_buffer_separately) {
  StateShadow s;
  SR_CHECK(s.SetVertexBuffer(0,Buffer(1),0));SR_CHECK(!s.SetVertexBuffer(0,Buffer(1),0));
  SR_CHECK(s.SetVertexBuffer(1,Buffer(1),0));            // another slot with the same buffer is still a new binding
  SR_CHECK(s.SetVertexBuffer(0,Buffer(1),16));           // same buffer, another offset
  SR_CHECK(s.SetVertexBuffer(0,Buffer(2),16));           // another buffer
  SR_CHECK(!s.SetVertexBuffer(1,Buffer(1),0));SR_CHECK(!s.SetVertexBuffer(0,Buffer(2),16));
  SR_CHECK(s.SetIndexBuffer(Buffer(5),0,VK_INDEX_TYPE_UINT32));SR_CHECK(!s.SetIndexBuffer(Buffer(5),0,VK_INDEX_TYPE_UINT32));
  SR_CHECK(s.SetIndexBuffer(Buffer(5),4,VK_INDEX_TYPE_UINT32));
  SR_CHECK(s.SetIndexBuffer(Buffer(6),4,VK_INDEX_TYPE_UINT32));
  SR_CHECK(s.SetIndexBuffer(Buffer(6),4,VK_INDEX_TYPE_UINT16));
}
SR_TEST(state_shadow_never_filters_a_vertex_slot_it_cannot_track) {
  StateShadow s;
  SR_CHECK(s.SetVertexBuffer(32,Buffer(1),0));SR_CHECK(s.SetVertexBuffer(32,Buffer(1),0));
}
SR_TEST(state_shadow_invalidate_forgets_everything) {
  StateShadow s;
  VkViewport viewport{0,0,1,1,0,1};VkRect2D scissor{{0,0},{1,1}};float blend[4]{};VkDescriptorSet sets[3]{Set(1),Set(2),Set(3)};
  s.SetViewport(viewport);s.SetScissor(scissor);s.SetBlend(blend);s.SetStencil(7);s.SetSharedSets(sets);
  s.SetVertexBuffer(3,Buffer(9),0);s.SetIndexBuffer(Buffer(8),0,VK_INDEX_TYPE_UINT32);
  s.Invalidate();
  SR_CHECK(s.SetViewport(viewport));SR_CHECK(s.SetScissor(scissor));SR_CHECK(s.SetBlend(blend));SR_CHECK(s.SetStencil(7));
  SR_CHECK(s.SetSharedSets(sets));SR_CHECK(s.SetVertexBuffer(3,Buffer(9),0));SR_CHECK(s.SetIndexBuffer(Buffer(8),0,VK_INDEX_TYPE_UINT32));
  SR_CHECK(s.enabled());
}
SR_TEST(state_shadow_disabled_always_emits_and_stays_disabled_after_invalidate) {
  StateShadow s(false);
  SR_CHECK(!s.enabled());
  VkViewport viewport{0,0,1,1,0,1};
  SR_CHECK(s.SetViewport(viewport));SR_CHECK(s.SetViewport(viewport));
  SR_CHECK(s.SetStencil(1));SR_CHECK(s.SetStencil(1));
  s.Invalidate();SR_CHECK(!s.enabled());SR_CHECK(s.SetStencil(1));
}
