#include "render_targets.h"
#include "test_main.h"
using namespace superman_returns::graphics::vulkan;
namespace guest=superman_returns::graphics::guest;
SR_TEST(attachments_drop_stale_mrt_and_cover_smaller_depth) {
  guest::DrawPacket d;
  d.colors[0]={1,false,{1280,720,0,0,0,720}};
  d.colors[1]={2,false,{256,144,100,0,0,16}};
  d.depth={3,true,{1024,576,1000,0,0,500}};
  d.registers[0x104]=0xf;
  Error e;auto p=PlanAttachments(d,e);
  SR_CHECK(e.message.empty());SR_CHECK_EQ(p.color_count,1u);SR_CHECK_EQ(p.colors[1].id,0u);
  SR_CHECK_EQ(p.extent.width,1280u);SR_CHECK_EQ(p.depth.geometry.width,1280u);
  SR_CHECK_EQ(p.depth.geometry.height,720u);SR_CHECK_EQ(p.depth.geometry.edram_tiles,500u);
}
SR_TEST(edram_alias_plan_observes_writes_outside_current_bind_set) {
  AliasSurface target{1,false,{64,64,20,0,0,16},1};
  std::vector<AliasSurface> all{{2,false,{64,64,20,12,0,16},4},
                                {3,true,{64,64,24,0,0,16},3}};
  auto p=PlanEdramAlias(target,all,{});
  SR_CHECK(p.action==AliasAction::kReinterpret);SR_CHECK_EQ(p.source,2u);
  all.back().last_write=5;p=PlanEdramAlias(target,all,{});
  SR_CHECK(p.action==AliasAction::kClear);
  std::array<uint64_t,1> bound{3};p=PlanEdramAlias(target,all,bound);
  SR_CHECK(p.action==AliasAction::kReinterpret);
  SR_CHECK(EdramOverlaps(2040,16,0,16));SR_CHECK(!EdramOverlaps(64,16,128,16));
}
SR_TEST(pass_plan_rejects_inconsistent_framebuffer_and_samples) {
  guest::DrawPacket d;d.colors[0]={1,false,{64,64,20,0,0,16}};
  Error e;auto p=PlanAttachments(d,e);SR_CHECK(ValidatePassPlan(p,e));
  p.extent.width=32;SR_CHECK(!ValidatePassPlan(p,e));
  p=PlanAttachments(d,e);p.color_count=0;SR_CHECK(!ValidatePassPlan(p,e));
  p=PlanAttachments(d,e);p.samples=VK_SAMPLE_COUNT_4_BIT;SR_CHECK(!ValidatePassPlan(p,e));
  p={};p.extent={64,64};SR_CHECK(!ValidatePassPlan(p,e));
}
SR_TEST(target_submission_rejects_completed_serial) {
  Context c;ImageState states(c.f);TargetStore store(c,states);Error e;
  auto command=reinterpret_cast<VkCommandBuffer>(uintptr_t(1));
  SR_CHECK(store.BeginSubmission(command,1,e));store.Retire(1);
  SR_CHECK(!store.BeginSubmission(command,1,e));
  SR_CHECK(store.BeginSubmission(command,2,e));
}
