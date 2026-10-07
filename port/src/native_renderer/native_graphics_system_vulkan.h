#pragma once
#include "native_graphics_system_interface.h"

namespace superman_returns::native {

class VulkanNativeGraphicsSystem : public INativeGraphicsSystem {
public:
    VulkanNativeGraphicsSystem();
    ~VulkanNativeGraphicsSystem() override;

    bool has_presentation() const override { return false; }
    rex::ui::GraphicsProvider* provider() const override { return nullptr; }
    rex::ui::Presenter* presenter() const override { return nullptr; }
    uint32_t guest_frame_counter() const override { return 0; }
    bool GetGammaRamp256(uint32_t* out_entries) const override { return false; }
    void SetInterruptCallback(uint32_t callback, uint32_t user_data) override {}
    void InitializeRingBuffer(uint32_t ptr, uint32_t size_log2) override {}
    void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size_log2) override {}
    void Shutdown() override {}

    void SignalGpuProgress() override {}
    uint64_t progress_generation() const override { return 0; }
    void WaitProgress(uint64_t since, uint32_t timeout_us) override {}

    rex::X_STATUS SetupPresentation(rex::ui::WindowedAppContext* app_context) override;
    rex::X_STATUS SetupGuestGpu(rex::runtime::FunctionDispatcher* function_dispatcher,
                           rex::system::KernelState* kernel_state) override;
};

} // namespace superman_returns::native
