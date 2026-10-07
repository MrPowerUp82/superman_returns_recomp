#include "native_graphics_system_vulkan.h"
#include <rex/logging.h>

namespace superman_returns::native {

VulkanNativeGraphicsSystem::VulkanNativeGraphicsSystem() {}
VulkanNativeGraphicsSystem::~VulkanNativeGraphicsSystem() {}

rex::X_STATUS VulkanNativeGraphicsSystem::SetupPresentation(rex::ui::WindowedAppContext* app_context) {
    REXLOG_INFO("VulkanNativeGraphicsSystem::SetupPresentation stub called");
    return rex::X_STATUS_SUCCESS;
}

rex::X_STATUS VulkanNativeGraphicsSystem::SetupGuestGpu(rex::runtime::FunctionDispatcher* function_dispatcher,
                       rex::system::KernelState* kernel_state) {
    REXLOG_INFO("VulkanNativeGraphicsSystem::SetupGuestGpu stub called");
    return rex::X_STATUS_SUCCESS;
}

} // namespace superman_returns::native
