#pragma once
#include <cstdint>
#include <rex/system/interfaces/graphics.h>

namespace rex::ui {
class GraphicsProvider;
class Presenter;
}

namespace superman_returns::native {

class INativeGraphicsSystem : public rex::system::IGraphicsSystem {
public:
    virtual ~INativeGraphicsSystem() = default;

    virtual bool has_presentation() const = 0;
    virtual rex::ui::GraphicsProvider* provider() const = 0;
    virtual rex::ui::Presenter* presenter() const = 0;
    virtual uint32_t guest_frame_counter() const = 0;
    virtual bool GetGammaRamp256(uint32_t* out_entries) const = 0;
    virtual void SetInterruptCallback(uint32_t callback, uint32_t user_data) = 0;
    virtual void InitializeRingBuffer(uint32_t ptr, uint32_t size_log2) = 0;
    virtual void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size_log2) = 0;
    virtual void Shutdown() = 0;

    virtual void SignalGpuProgress() = 0;
    virtual uint64_t progress_generation() const = 0;
    virtual void WaitProgress(uint64_t since, uint32_t timeout_us) = 0;
};

// Returns the active instance
INativeGraphicsSystem* ActiveNativeGraphicsSystem();
uint64_t GpuProgressGeneration();
void WaitForGpuProgress(uint64_t since, uint32_t timeout_us);

} // namespace superman_returns::native
