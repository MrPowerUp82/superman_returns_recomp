#include "test_main.h"
#include "../../port/src/graphics/ray_shadow_execution.h"
using namespace superman_returns::graphics;
SR_TEST(ray_shadow_applied_requires_executed_pixel_shader_counter_and_completed_queue_fence) {
  RayShadowExecution state;
  GraphicsValues values{1,2,0,0,0};
  SR_CHECK(state.Submit(0,4,10,3,values,true));
  SR_CHECK(!state.Complete(0,3,128));
  SR_CHECK_EQ(state.Latest().applied[0],0);
  SR_CHECK(state.Complete(0,4,0)); // CPU draw alone does not prove p0/ray branch execution.
  SR_CHECK_EQ(state.Latest().applied[0],0);
  SR_CHECK(state.Submit(1,5,11,3,values,true));
  SR_CHECK(state.Complete(1,5,128));
  SR_CHECK_EQ(state.Latest().applied[0],1); SR_CHECK_EQ(state.Latest().applied[1],2);
  SR_CHECK_EQ(state.Latest().revision,3u);
}
SR_TEST(ray_shadow_newer_off_cannot_be_resurrected_by_late_old_completion) {
  RayShadowExecution state;
  SR_CHECK(!state.Submit(0,0,1,1,{1,1,0,0,0},true));
  SR_CHECK(state.Submit(0,2,2,1,{1,1,0,0,0},true));
  SR_CHECK(!state.Submit(0,3,3,2,{0,1,0,0,0},false)); // In-flight slot reuse forbidden.
  SR_CHECK(state.Submit(1,3,3,2,{0,1,0,0,0},false));
  SR_CHECK(state.Complete(1,3,999));
  SR_CHECK_EQ(state.Latest().applied[0],0);
  SR_CHECK(state.Complete(0,3,999));
  SR_CHECK_EQ(state.Latest().frame,3u); SR_CHECK_EQ(state.Latest().revision,2u);
  SR_CHECK_EQ(state.Latest().applied[0],0);
}

SR_TEST(ray_shadow_character_row_needs_both_the_request_and_a_character_blas) {
  const auto applied=[](int requested,bool built) {
    superman_returns::graphics::RayShadowExecution execution;
    superman_returns::graphics::GraphicsValues values{1,1,requested,0,0};
    SR_CHECK(execution.Submit(0,1,1,1,values,true,built));
    SR_CHECK(execution.Complete(0,1,10));
    return execution.Latest().applied[2];
  };
  SR_CHECK_EQ(applied(1,true),1);
  SR_CHECK_EQ(applied(1,false),0);
  SR_CHECK_EQ(applied(0,true),0);
}
