#include "composition.h"
#include "test_main.h"
using namespace superman_returns::graphics::vulkan;
SR_TEST(frontbuffer_composition_letterboxes_without_distorting_guest_aspect) {
  auto box=PlanPresentRect({800,800},16,9);
  SR_CHECK_EQ(box.offset.x,0);SR_CHECK_EQ(box.offset.y,175);SR_CHECK_EQ(box.extent.width,800u);SR_CHECK_EQ(box.extent.height,450u);
  box=PlanPresentRect({1920,720},16,9);SR_CHECK_EQ(box.offset.x,320);SR_CHECK_EQ(box.extent.width,1280u);
  box=PlanPresentRect({0,800},16,9);SR_CHECK_EQ(box.extent.width,0u);
}
