#include "test_main.h"
#include "../../port/src/graphics/object_motion_frame.h"
using namespace superman_returns::graphics;
static void Observe(ObjectMotionFrame& frame,bool captured,bool matched) {
  const uint64_t serial=frame.draws()+1;
  frame.ExpectDraw(serial);
  if(captured) frame.CaptureDraw(serial,matched,true);
}

SR_TEST(object_motion_recovers_after_raster_frame_without_disabling_collection) {
  ObjectMotionFrame f;
  f.Begin(1,1280,720);Observe(f,true,false);
  SR_CHECK(f.Rejection()==ObjectMotionRejection::kCorrespondence);
  f.Begin(2,1280,720);Observe(f,true,true);
  SR_CHECK(f.Rejection()==ObjectMotionRejection::kNone);
  f.Begin(3,1280,720); // Never reuse last frame's complete result.
  SR_CHECK(f.Rejection()==ObjectMotionRejection::kNoScene);
}
SR_TEST(object_motion_rejects_any_uncaptured_or_unverified_contributor) {
  ObjectMotionFrame f;f.Begin(1,1280,720);Observe(f,true,true);
  Observe(f,false,false);Observe(f,true,true);
  SR_CHECK(f.Rejection()==ObjectMotionRejection::kCapture);
  SR_CHECK_EQ(f.draws(),3u);SR_CHECK_EQ(f.captured(),2u);SR_CHECK_EQ(f.matched(),2u);
  f.Begin(2,1280,720);Observe(f,true,false);Observe(f,true,true);
  SR_CHECK(f.Rejection()==ObjectMotionRejection::kCorrespondence);
  f.Fail(ObjectMotionRejection::kCapacity);
  SR_CHECK(f.Rejection()==ObjectMotionRejection::kCapacity);
}
SR_TEST(object_motion_invalid_dimensions_and_explicit_failure_never_authorize_ngx) {
  ObjectMotionFrame f;f.Begin(1,0,720);Observe(f,true,true);
  SR_CHECK(f.Rejection()!=ObjectMotionRejection::kNone);
  f.Begin(2,1280,720);Observe(f,true,true);f.Fail(ObjectMotionRejection::kCamera);
  SR_CHECK(f.Rejection()==ObjectMotionRejection::kCamera);
}
SR_TEST(object_motion_cannot_cancel_missing_color_draw_with_extra_depth_draw) {
  ObjectMotionFrame f;f.Begin(1,1280,720);
  f.ExpectDraw(10);f.ExpectDraw(20);
  f.CaptureDraw(10,true,true);f.CaptureDraw(30,true,true);
  SR_CHECK(f.Rejection()!=ObjectMotionRejection::kNone);
  f.Begin(2,1280,720);f.ExpectDraw(40);f.ExpectDraw(50);
  f.CaptureDraw(50,true,true);f.CaptureDraw(40,true,true);
  SR_CHECK(f.Rejection()==ObjectMotionRejection::kNone);
}
SR_TEST(object_motion_ownership_does_not_authorize_blended_layers) {
  ObjectMotionFrame f;f.Begin(1,1280,720);f.ExpectDraw(10);
  f.CaptureDraw(10,true,false);
  SR_CHECK(f.Rejection()==ObjectMotionRejection::kCoverage);
}
SR_TEST(object_motion_missing_capture_outranks_blended_coverage_in_either_order) {
  ObjectMotionFrame f;
  for(bool blend_first:{false,true}) {
    f.Begin(1,1280,720);f.ExpectDraw(10);f.ExpectDraw(20);
    f.CaptureDraw(blend_first?10:20,true,!blend_first);
    // The other expected draw was never captured: a blend cannot hide the gap.
    SR_CHECK(f.Rejection()==ObjectMotionRejection::kCapture);
    f.CaptureDraw(blend_first?20:10,true,blend_first);
    SR_CHECK(f.Rejection()==ObjectMotionRejection::kCoverage);
  }
  f.Begin(2,1280,720);f.ExpectDraw(30);f.CaptureDraw(30,true,true);
  SR_CHECK(f.Rejection()==ObjectMotionRejection::kNone);
}
SR_TEST(object_motion_identity_uv_rejects_flip_scale_offset_and_perspective) {
  const std::array<std::array<float,4>,3> clips{{{-1,-1,0,1},{-1,3,0,1},{3,-1,0,1}}};
  const std::array<std::array<float,2>,3> uv{{{0,1},{0,-1},{2,1}}};
  SR_CHECK(ProveObjectMotionIdentityUv(clips,uv));
  auto changed=uv;changed[0][0]=1;
  SR_CHECK(!ProveObjectMotionIdentityUv(clips,changed));
  changed=uv;changed[2][0]=1;
  SR_CHECK(!ProveObjectMotionIdentityUv(clips,changed));
  auto perspective=clips;perspective[1][3]=2;
  SR_CHECK(!ProveObjectMotionIdentityUv(perspective,uv));
}
SR_TEST(object_motion_camera_jump_rejects_teleport_and_singular_view) {
  const std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  auto camera=identity;camera[3]=.02f;
  SR_CHECK(!ObjectMotionCameraJump(identity,camera));
  camera[3]=4;
  SR_CHECK(ObjectMotionCameraJump(identity,camera));
  camera={};SR_CHECK(ObjectMotionCameraJump(identity,camera));
}
SR_TEST(object_motion_cpu_correspondence_cannot_attest_gpu_pixel_validity) {
  SR_CHECK(ObjectMotionConsumerRejection(ObjectMotionRejection::kNone,true,false)==ObjectMotionRejection::kPixelValidity);
  SR_CHECK(ObjectMotionConsumerRejection(ObjectMotionRejection::kNone,false,true)==ObjectMotionRejection::kLineage);
  SR_CHECK(ObjectMotionConsumerRejection(ObjectMotionRejection::kCoverage,true,true)==ObjectMotionRejection::kCoverage);
  SR_CHECK(ObjectMotionConsumerRejection(ObjectMotionRejection::kNone,true,true)==ObjectMotionRejection::kNone);
}
namespace {
MotionContentVersion Version(uintptr_t resource,uint64_t serial=1,uint64_t life=1) {
  return {resource,life,5,serial,1280,720};
}
MotionSceneTicket Ticket() {return {5,7,9,1280,720};}
}
SR_TEST(motion_lineage_follows_exact_versions_through_full_copy_and_audited_pass) {
  MotionColorLineage lineage;lineage.Begin(5);
  auto hdr=Version(100),resolved=Version(200,2),tone=Version(300,3),filtered=Version(400,4);
  SR_CHECK(lineage.Seed(hdr,Ticket()));
  SR_CHECK(lineage.Propagate(hdr,resolved,true));
  SR_CHECK(lineage.Propagate(resolved,tone,true));
  SR_CHECK(lineage.Propagate(tone,filtered,true));
  SR_CHECK(lineage.Find(filtered)==Ticket());
  auto wrong_version=filtered;wrong_version.serial=5;
  SR_CHECK(!lineage.Find(wrong_version));
  auto wrong_life=filtered;wrong_life.lifetime=2;
  SR_CHECK(!lineage.Find(wrong_life));
  lineage.Begin(6);SR_CHECK(!lineage.Find(filtered));
}
SR_TEST(motion_lineage_unknown_or_partial_write_invalidates_old_destination) {
  MotionColorLineage lineage;lineage.Begin(5);
  auto hdr=Version(100),target=Version(200,2);
  SR_CHECK(lineage.Seed(hdr,Ticket()));SR_CHECK(lineage.Propagate(hdr,target,true));
  auto changed=target;changed.serial=3;
  SR_CHECK(!lineage.Propagate(hdr,changed,false));
  SR_CHECK(!lineage.Find(target));SR_CHECK(!lineage.Find(changed));
  SR_CHECK(lineage.Propagate(hdr,changed,true));
  lineage.Invalidate(changed.resource);
  SR_CHECK(!lineage.Find(changed));
  auto scaled=Version(300,4);scaled.width=640;
  SR_CHECK(!lineage.Propagate(hdr,scaled,true));
  SR_CHECK(!lineage.Propagate(hdr,hdr,true)); // Cannot read and overwrite the same subresource.
  SR_CHECK(!lineage.Find(hdr));
}
SR_TEST(motion_lineage_rejects_stale_frame_and_content_recycling) {
  MotionColorLineage lineage;lineage.Begin(5);
  auto hdr=Version(100);SR_CHECK(lineage.Seed(hdr,Ticket()));
  auto reused=hdr;reused.lifetime=2;reused.serial=2;
  SR_CHECK(lineage.Seed(reused,Ticket()));SR_CHECK(!lineage.Find(hdr));
  auto previous=reused;previous.frame=4;
  SR_CHECK(!lineage.Find(previous));
  auto stale=Ticket();stale.frame=4;
  SR_CHECK(!lineage.Seed(Version(200),stale));
}
SR_TEST(motion_lineage_resolves_copies_recorded_before_depth_epoch_seals) {
  MotionColorLineage lineage;lineage.Begin(5);
  auto hdr=Version(100),resolved=Version(200,2),tone=Version(300,3);
  lineage.Propagate(hdr,resolved,true);
  lineage.Propagate(resolved,tone,true);
  SR_CHECK(!lineage.Find(tone));
  SR_CHECK(lineage.Seed(hdr,Ticket()));
  SR_CHECK(lineage.Find(tone)==Ticket());
  lineage.Begin(5);
  lineage.Propagate(hdr,resolved,true);
  lineage.Invalidate(resolved.resource);
  SR_CHECK(lineage.Seed(hdr,Ticket()));
  SR_CHECK(!lineage.Find(resolved));
  lineage.Begin(5);
  lineage.Propagate(hdr,resolved,true);
  ++hdr.serial;
  SR_CHECK(lineage.Seed(hdr,Ticket()));
  SR_CHECK(!lineage.Find(resolved)); // Earlier refraction copy is not the final scene.
}
