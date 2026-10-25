#pragma once

#include <cstdint>
#include <memory>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
struct ID3D12Fence;

namespace superman_returns::graphics::d3d12 {

enum class DlssRequested { Off, Quality };
enum class DlssSupport { NotQueried, SdkUnavailable, InitializationFailed, Unsupported, Supported };
enum class DlssApplied { Off, PendingGpu, Quality };
enum class DlssDepth { Unknown, Forward, Reversed }; // Device depth [0,1], not linear/view-space Z.
enum class DlssMotion { Unknown, CurrentToPreviousPixels }; // After multiplication by mvScaleX/Y.
enum class DlssError {
    None, SdkUnavailable, NotInitialized, NotRequested, InvalidPath, InvalidDevice,
    AlreadyInitialized, Unsupported, DriverUpdateRequired, SdkFailure,
    InvalidDimensions, UnverifiedTemporalInputs, UnsupportedConventions,
    InvalidScalars, InvalidResources, InvalidResourceStates, RequiresShutdown,
    InvalidTicket, SubmissionRequired, GpuPending, InvalidFence, DeviceRemoved
};

struct DlssResult {
    DlssError error = DlssError::None;
    std::uint32_t ngxResult = 0; // Raw NVSDK_NGX_Result, zero if no SDK call was made.
    std::uint64_t ticket = 0;   // Only Evaluate success returns a nonzero ticket.
    explicit operator bool() const { return error == DlssError::None; }
};

// Caller attests semantic facts established from actual renderer data. These default
// to unverified: a non-null depth/velocity texture is not evidence of correct contents.
struct DlssFrameContract {
    std::uint32_t renderWidth = 0, renderHeight = 0;
    std::uint32_t outputWidth = 0, outputHeight = 0;
    bool conventionsVerified = false;
    bool colorBeforeHud = false;
    bool rasterJitterApplied = false;
    bool motionIncludesCameraObjectsAndDeformation = false;
    DlssDepth depth = DlssDepth::Unknown;
    DlssMotion motion = DlssMotion::Unknown;
    bool hdr = false;
    bool motionJittered = false;
    float jitterX = 0, jitterY = 0; // Actual projection jitter in render pixels, +right/+down.
    float mvScaleX = 0, mvScaleY = 0; // Convert stored vectors to render pixels, +right/+down.
    float preExposure = 1;           // Multiplicative exposure already applied to input color.
    bool reset = false;              // Cut/load/teleport/identity invalidation, etc.
};

// Pure validation; usable without Windows, an NVIDIA SDK or a GPU.
DlssError ValidateDlssFrame(const DlssFrameContract& frame);

struct DlssOptimalSettings {
    std::uint32_t outputWidth = 0, outputHeight = 0;
    std::uint32_t renderWidth = 0, renderHeight = 0;
    std::uint32_t minWidth = 0, minHeight = 0, maxWidth = 0, maxHeight = 0;
    float recommendedSharpness = 0; // Reported only; adapter does not enable sharpening.
};

struct DlssEvaluation {
    ID3D12GraphicsCommandList* commands = nullptr; // Open DIRECT list on Initialize's device.
    ID3D12Resource* color = nullptr;
    ID3D12Resource* output = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* motion = nullptr;
    // Exact ALL_SUBRESOURCES states: NON_PIXEL_SHADER_RESOURCE for every input and
    // UNORDERED_ACCESS for output. No combined read states. SDK restores these states.
    std::uint32_t colorState = 0, outputState = 0, depthState = 0, motionState = 0;
    DlssFrameContract frame;
};

struct DlssStatus {
    DlssRequested requested = DlssRequested::Off;
    DlssSupport support = DlssSupport::NotQueried;
    DlssApplied applied = DlssApplied::Off;
    DlssError lastError = DlssError::None;
    std::uint32_t lastNgxResult = 0;
    std::uint64_t pendingTicket = 0, completedTicket = 0;
};

// One instance owns NGX D3D12 on one single-node device. Serialize all calls on the
// render thread; no other NGX owner for this device. No interposer/presenter hooks.
class DlssSuperResolution final {
public:
    DlssSuperResolution();
    // MUST Shutdown after a covering renderer fence before destruction. On misuse,
    // debug asserts; release intentionally retains live GPU dependencies to avoid UAF.
    ~DlssSuperResolution();
    DlssSuperResolution(const DlssSuperResolution&) = delete;
    DlssSuperResolution& operator=(const DlssSuperResolution&) = delete;

    void SetRequested(DlssRequested requested);
    // Existing absolute writable DIRECTORY used for NGX data and SDK DLL search.
    // Identity is this custom engine's own project GUID, not an NVIDIA Application ID.
    DlssResult Initialize(ID3D12Device* device, const wchar_t* applicationPath);
    DlssResult QueryOptimalSettings(std::uint32_t outputWidth, std::uint32_t outputHeight,
                                   DlssOptimalSettings& out);
    // Requires queried exact Quality dimensions. First valid evaluation also creates
    // the NGX feature. Size/format/creation-flag changes require fenced Shutdown+Init.
    // All calls into NGX may invalidate PSO/root/descriptors; caller rebinds its state.
    DlssResult Evaluate(const DlssEvaluation& evaluation);
    // After successful ExecuteCommandLists + queue Signal, notify the exact ticket.
    // Required before the next Evaluate. One DIRECT queue/fence for this instance,
    // increasing nonzero fence values. Does not wait or claim that GPU work executed.
    DlssResult NotifySubmitted(std::uint64_t ticket, ID3D12Fence* fence, std::uint64_t value);
    // Poll submitted tickets (including older frames). Only a completed real device
    // fence can apply Quality. Old tickets across Off/failure/Shutdown cannot apply.
    DlssResult ConfirmExecution(std::uint64_t ticket, ID3D12Fence* fence, std::uint64_t value);
    // Fence must cover ALL recorded feature creation/evaluation, even failed SDK calls.
    // No fence needed if no GPU work was ever recorded. Refuses pending/invalid fences.
    DlssResult Shutdown(ID3D12Fence* fence = nullptr, std::uint64_t value = 0);
    const DlssStatus& GetStatus() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace superman_returns::graphics::d3d12
