#include "dlss_super_resolution.h"

#include <cassert>
#include <cmath>
#include <limits>

#ifndef SR_HAS_DLSS
#define SR_HAS_DLSS 0
#endif

#if SR_HAS_DLSS
#ifndef _WIN32
#error SR_HAS_DLSS requires Windows D3D12
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <wrl/client.h>
#include <filesystem>
#include <string>
#include <deque>
#include <algorithm>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#endif

namespace superman_returns::graphics::d3d12 {

DlssError ValidateDlssFrame(const DlssFrameContract& f) {
    // This adapter deliberately supports fixed-resolution SR only, never DLAA.
    if (!f.renderWidth || !f.renderHeight || !f.outputWidth || !f.outputHeight ||
        f.renderWidth >= f.outputWidth || f.renderHeight >= f.outputHeight ||
        f.outputWidth > 16384 || f.outputHeight > 16384)
        return DlssError::InvalidDimensions;
    if (!f.conventionsVerified || !f.colorBeforeHud || !f.rasterJitterApplied ||
        (!f.motionIncludesCameraObjectsAndDeformation && !f.motionCameraOnlyAccepted))
        return DlssError::UnverifiedTemporalInputs;
    if ((f.depth != DlssDepth::Forward && f.depth != DlssDepth::Reversed) ||
        f.motion != DlssMotion::CurrentToPreviousPixels)
        return DlssError::UnsupportedConventions;
    if (!std::isfinite(f.jitterX) || !std::isfinite(f.jitterY) ||
        std::abs(f.jitterX) > 0.5f || std::abs(f.jitterY) > 0.5f ||
        !std::isfinite(f.mvScaleX) || !std::isfinite(f.mvScaleY) ||
        f.mvScaleX == 0 || f.mvScaleY == 0 ||
        !std::isfinite(f.preExposure) || f.preExposure <= 0)
        return DlssError::InvalidScalars;
    return DlssError::None;
}

struct DlssSuperResolution::Impl {
    DlssStatus status;
    DlssOptimalSettings optimal;
    std::uint64_t nextTicket = 0; // Never reset at Shutdown: stale tickets stay stale.
    std::uint64_t generation = 1, unsubmittedTicket = 0, unsubmittedGeneration = 0;
    bool initialized = false, gpuWorkRecorded = false, resetHistory = true;
    bool teardownOnly = false;
#if SR_HAS_DLSS
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    NVSDK_NGX_Parameter* parameters = nullptr;
    NVSDK_NGX_Handle* feature = nullptr;
    std::wstring applicationPath;
    DlssFrameContract featureContract;
    DXGI_FORMAT colorFormat = DXGI_FORMAT_UNKNOWN, depthFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT motionFormat = DXGI_FORMAT_UNKNOWN;
    struct Submission {
        std::uint64_t ticket, generation, value;
    };
    std::deque<Submission> submissions;
    Microsoft::WRL::ComPtr<ID3D12Fence> submissionFence;
    std::uint64_t lastSubmittedValue = 0;
#endif
    DlssResult Result(DlssError error, std::uint32_t ngx = 0, std::uint64_t ticket = 0) {
        status.lastError = error;
        status.lastNgxResult = ngx;
        return {error, ngx, ticket};
    }
    void Invalidate() {
        status.applied = DlssApplied::Off;
        status.pendingTicket = 0;
        resetHistory = true;
        ++generation;
    }
#if SR_HAS_DLSS
    DlssError CheckFence(ID3D12Fence* fence, std::uint64_t value) const {
        if (!fence || !value || value == std::numeric_limits<std::uint64_t>::max())
            return DlssError::InvalidFence;
        Microsoft::WRL::ComPtr<ID3D12Device> owner;
        if (FAILED(fence->GetDevice(IID_PPV_ARGS(&owner))) || owner.Get() != device.Get())
            return DlssError::InvalidFence;
        const auto completed = fence->GetCompletedValue();
        if (completed == std::numeric_limits<std::uint64_t>::max()) return DlssError::DeviceRemoved;
        return completed >= value ? DlssError::None : DlssError::GpuPending;
    }
    bool Owns(ID3D12Resource* resource) const {
        Microsoft::WRL::ComPtr<ID3D12Device> owner;
        return SUCCEEDED(resource->GetDevice(IID_PPV_ARGS(&owner))) && owner.Get() == device.Get();
    }
    bool Texture(ID3D12Resource* r, std::uint32_t width, std::uint32_t height, bool output) const {
        if (!Owns(r)) return false;
        const auto d = r->GetDesc();
        return d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
            d.Width == width && d.Height == height && d.DepthOrArraySize == 1 &&
            d.MipLevels == 1 && d.SampleDesc.Count == 1 && d.SampleDesc.Quality == 0 &&
            (output ? (d.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0
                    : (d.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) == 0);
    }
#endif
};

DlssSuperResolution::DlssSuperResolution() : impl_(std::make_unique<Impl>()) {}
DlssSuperResolution::~DlssSuperResolution() {
    if (impl_->gpuWorkRecorded) {
        assert(false && "Call DlssSuperResolution::Shutdown after the renderer fence");
        (void)impl_.release(); // Retain NGX/device dependencies instead of freeing in-flight work.
    } else {
        (void)Shutdown();
    }
}

void DlssSuperResolution::SetRequested(DlssRequested requested) {
    if (requested != impl_->status.requested) impl_->Invalidate();
    impl_->status.requested = requested;
}

DlssResult DlssSuperResolution::Initialize(ID3D12Device* device, const wchar_t* applicationPath) {
    auto& p = *impl_;
#if !SR_HAS_DLSS
    (void)device; (void)applicationPath;
    p.status.support = DlssSupport::SdkUnavailable;
    p.Invalidate();
    return p.Result(DlssError::SdkUnavailable);
#else
    if (p.initialized || p.teardownOnly) return p.Result(DlssError::AlreadyInitialized);
    p.Invalidate();
    p.status.support = DlssSupport::InitializationFailed;
    if (!device || device->GetNodeCount() != 1) return p.Result(DlssError::InvalidDevice);
    if (!applicationPath || !*applicationPath) return p.Result(DlssError::InvalidPath);
    std::error_code ec;
    const std::filesystem::path path(applicationPath);
    if (!path.is_absolute() || !std::filesystem::is_directory(path, ec) || ec)
        return p.Result(DlssError::InvalidPath);
    p.applicationPath = path.wstring();
    const wchar_t* paths[] = {p.applicationPath.c_str()};
    NVSDK_NGX_FeatureCommonInfo common{};
    common.PathListInfo.Path = paths;
    common.PathListInfo.Length = 1;
    // Locally assigned custom-engine identity; no registration/borrowed App ID.
    auto result = NVSDK_NGX_D3D12_Init_with_ProjectID(
        "66af6a9a-8ff1-4eb4-b517-56bfecb291ae", NVSDK_NGX_ENGINE_TYPE_CUSTOM,
        "superman_returns_recomp-1", p.applicationPath.c_str(), device, &common);
    if (NVSDK_NGX_FAILED(result)) return p.Result(DlssError::SdkFailure, result);
    p.initialized = true;
    p.device = device;
    result = NVSDK_NGX_D3D12_GetCapabilityParameters(&p.parameters);
    if (NVSDK_NGX_FAILED(result)) {
        (void)Shutdown();
        p.status.support = DlssSupport::InitializationFailed;
        return p.Result(DlssError::SdkFailure, result);
    }
    int available = 0, needsDriver = 0;
    const auto availableResult = p.parameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    const auto driverResult = p.parameters->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsDriver);
    if (NVSDK_NGX_FAILED(availableResult) || !available ||
        (NVSDK_NGX_SUCCEED(driverResult) && needsDriver)) {
        const bool driver = NVSDK_NGX_SUCCEED(driverResult) && needsDriver;
        (void)Shutdown();
        p.status.support = DlssSupport::Unsupported;
        return p.Result(driver ? DlssError::DriverUpdateRequired : DlssError::Unsupported, availableResult);
    }
    p.status.support = DlssSupport::Supported;
    return p.Result(DlssError::None, result);
#endif
}

DlssResult DlssSuperResolution::QueryOptimalSettings(std::uint32_t width, std::uint32_t height,
                                                     DlssOptimalSettings& out) {
    auto& p = *impl_;
    out = {};
#if !SR_HAS_DLSS
    (void)width; (void)height;
    return p.Result(DlssError::SdkUnavailable);
#else
    if (!p.initialized || p.teardownOnly || !p.parameters || p.status.support != DlssSupport::Supported)
        return p.Result(DlssError::NotInitialized);
    if (!width || !height || width > 16384 || height > 16384)
        return p.Result(DlssError::InvalidDimensions);
    if (p.feature && (width != p.optimal.outputWidth || height != p.optimal.outputHeight))
        return p.Result(DlssError::RequiresShutdown);
    DlssOptimalSettings settings{};
    settings.outputWidth = width; settings.outputHeight = height;
    const auto result = NGX_DLSS_GET_OPTIMAL_SETTINGS(p.parameters, width, height,
        NVSDK_NGX_PerfQuality_Value_MaxQuality, &settings.renderWidth, &settings.renderHeight,
        &settings.maxWidth, &settings.maxHeight, &settings.minWidth, &settings.minHeight,
        &settings.recommendedSharpness);
    if (NVSDK_NGX_FAILED(result)) return p.Result(DlssError::SdkFailure, result);
    if (!settings.renderWidth || !settings.renderHeight || settings.renderWidth >= width ||
        settings.renderHeight >= height || !settings.minWidth || !settings.minHeight ||
        settings.minWidth > settings.renderWidth || settings.minHeight > settings.renderHeight ||
        settings.maxWidth < settings.renderWidth || settings.maxHeight < settings.renderHeight ||
        !std::isfinite(settings.recommendedSharpness))
        return p.Result(DlssError::InvalidDimensions, result);
    // Do not mutate an existing feature's dimensions even if driver policy changes.
    if (p.feature && (settings.renderWidth != p.optimal.renderWidth ||
                      settings.renderHeight != p.optimal.renderHeight))
        return p.Result(DlssError::RequiresShutdown);
    out = p.optimal = settings;
    return p.Result(DlssError::None, result);
#endif
}

DlssResult DlssSuperResolution::Evaluate(const DlssEvaluation& e) {
    auto& p = *impl_;
    // Any rejected/failed frame must use raster, and the next valid one resets history.
    const auto reject = [&](DlssError error, std::uint32_t ngx = 0) {
        p.Invalidate();
        return p.Result(error, ngx);
    };
#if !SR_HAS_DLSS
    (void)e;
    return reject(DlssError::SdkUnavailable);
#else
    // Never consume a feature/history whose creation/evaluation list was abandoned.
    // NotifySubmitted establishes queue order without forcing a CPU/GPU wait.
    if (p.unsubmittedTicket) return p.Result(DlssError::SubmissionRequired);
    if (p.submissions.size() >= 64) return p.Result(DlssError::GpuPending);
    if (p.status.requested != DlssRequested::Quality) return reject(DlssError::NotRequested);
    if (!p.initialized || p.teardownOnly || !p.parameters || p.status.support != DlssSupport::Supported)
        return reject(DlssError::NotInitialized);
    const auto& f = e.frame;
    if (const auto error = ValidateDlssFrame(f); error != DlssError::None) return reject(error);
    if (f.renderWidth != p.optimal.renderWidth || f.renderHeight != p.optimal.renderHeight ||
        f.outputWidth != p.optimal.outputWidth || f.outputHeight != p.optimal.outputHeight)
        return reject(DlssError::InvalidDimensions);
    if (!e.commands || !e.color || !e.output || !e.depth || !e.motion ||
        e.color == e.output || e.color == e.depth || e.color == e.motion ||
        e.output == e.depth || e.output == e.motion || e.depth == e.motion)
        return reject(DlssError::InvalidResources);
    Microsoft::WRL::ComPtr<ID3D12Device> listDevice;
    if (e.commands->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT ||
        FAILED(e.commands->GetDevice(IID_PPV_ARGS(&listDevice))) || listDevice.Get() != p.device.Get() ||
        !p.Texture(e.color, f.renderWidth, f.renderHeight, false) ||
        !p.Texture(e.depth, f.renderWidth, f.renderHeight, false) ||
        !p.Texture(e.motion, f.renderWidth, f.renderHeight, false) ||
        !p.Texture(e.output, f.outputWidth, f.outputHeight, true))
        return reject(DlssError::InvalidResources);
    const auto color = e.color->GetDesc().Format, output = e.output->GetDesc().Format;
    const auto depth = e.depth->GetDesc().Format, motion = e.motion->GetDesc().Format;
    const bool floatColor = color == DXGI_FORMAT_R16G16B16A16_FLOAT || color == DXGI_FORMAT_R11G11B10_FLOAT;
    if (color != output || (!floatColor && color != DXGI_FORMAT_R8G8B8A8_UNORM) ||
        (f.hdr && !floatColor) ||
        (depth != DXGI_FORMAT_R32_FLOAT && depth != DXGI_FORMAT_D32_FLOAT && depth != DXGI_FORMAT_R32_TYPELESS) ||
        (motion != DXGI_FORMAT_R16G16_FLOAT && motion != DXGI_FORMAT_R32G32_FLOAT))
        return reject(DlssError::InvalidResources);
    D3D12_FEATURE_DATA_FORMAT_SUPPORT format{output};
    if (FAILED(p.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &format, sizeof(format))) ||
        !(format.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))
        return reject(DlssError::InvalidResources);
    // NGX restores only its known states, not arbitrary caller read combinations.
    constexpr auto input = static_cast<std::uint32_t>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (e.colorState != input || e.depthState != input || e.motionState != input ||
        e.outputState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        return reject(DlssError::InvalidResourceStates);
    if (p.feature && (f.depth != p.featureContract.depth || f.hdr != p.featureContract.hdr ||
        f.motionJittered != p.featureContract.motionJittered || color != p.colorFormat ||
        depth != p.depthFormat || motion != p.motionFormat))
        return reject(DlssError::RequiresShutdown);
    // NGX can record work even when it returns an error. Preserve dependencies until fence.
    p.gpuWorkRecorded = true;
    if (!p.feature) {
        NVSDK_NGX_DLSS_Create_Params create{};
        create.Feature.InWidth = f.renderWidth; create.Feature.InHeight = f.renderHeight;
        create.Feature.InTargetWidth = f.outputWidth; create.Feature.InTargetHeight = f.outputHeight;
        create.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_MaxQuality;
        create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                                      NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
        if (f.hdr) create.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
        if (f.depth == DlssDepth::Reversed) create.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
        if (f.motionJittered) create.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVJittered;
        const auto result = NGX_D3D12_CREATE_DLSS_EXT(e.commands, 1, 1, &p.feature, p.parameters, &create);
        if (NVSDK_NGX_FAILED(result)) {
            p.teardownOnly = true; // Partial SDK creation cannot be retried safely.
            return reject(DlssError::SdkFailure, result);
        }
        p.featureContract = f;
        p.colorFormat = color; p.depthFormat = depth; p.motionFormat = motion;
    }
    NVSDK_NGX_D3D12_DLSS_Eval_Params eval{};
    eval.Feature.pInColor = e.color; eval.Feature.pInOutput = e.output;
    eval.pInDepth = e.depth; eval.pInMotionVectors = e.motion;
    eval.InJitterOffsetX = f.jitterX; eval.InJitterOffsetY = f.jitterY;
    eval.InMVScaleX = f.mvScaleX; eval.InMVScaleY = f.mvScaleY;
    eval.InRenderSubrectDimensions.Width = f.renderWidth;
    eval.InRenderSubrectDimensions.Height = f.renderHeight;
    eval.InReset = f.reset || p.resetHistory;
    eval.InPreExposure = f.preExposure;
    eval.InExposureScale = 1; // No scene exposure identified: documented autoexposure mode.
    const auto result = NGX_D3D12_EVALUATE_DLSS_EXT(e.commands, p.feature, p.parameters, &eval);
    if (NVSDK_NGX_FAILED(result)) {
        p.teardownOnly = true; // No success ticket exists to establish submission.
        return reject(DlssError::SdkFailure, result);
    }
    p.status.pendingTicket = ++p.nextTicket;
    p.unsubmittedTicket = p.status.pendingTicket;
    p.unsubmittedGeneration = p.generation;
    p.status.applied = DlssApplied::PendingGpu;
    return p.Result(DlssError::None, result, p.status.pendingTicket);
#endif
}

DlssResult DlssSuperResolution::NotifySubmitted(std::uint64_t ticket, ID3D12Fence* fence,
                                               std::uint64_t value) {
    auto& p = *impl_;
#if !SR_HAS_DLSS
    (void)ticket; (void)fence; (void)value;
    return p.Result(DlssError::SdkUnavailable);
#else
    if (!ticket || ticket != p.unsubmittedTicket) return p.Result(DlssError::InvalidTicket);
    const auto error = p.CheckFence(fence, value);
    if (error != DlssError::None && error != DlssError::GpuPending) return p.Result(error);
    if ((p.submissionFence && p.submissionFence.Get() != fence) || value <= p.lastSubmittedValue)
        return p.Result(DlssError::InvalidFence);
    p.submissionFence = fence;
    p.lastSubmittedValue = value;
    p.submissions.push_back({ticket, p.unsubmittedGeneration, value});
    // Queue order now guarantees earlier history initialization precedes reuse.
    if (p.unsubmittedGeneration == p.generation) p.resetHistory = false;
    p.unsubmittedTicket = 0;
    return p.Result(DlssError::None);
#endif
}

DlssResult DlssSuperResolution::ConfirmExecution(std::uint64_t ticket, ID3D12Fence* fence,
                                                std::uint64_t value) {
    auto& p = *impl_;
#if !SR_HAS_DLSS
    (void)ticket; (void)fence; (void)value;
    return p.Result(DlssError::SdkUnavailable);
#else
    const auto submitted = std::find_if(p.submissions.begin(), p.submissions.end(),
        [ticket](const Impl::Submission& s) { return ticket && s.ticket == ticket; });
    if (submitted == p.submissions.end())
        return p.Result(DlssError::InvalidTicket);
    if (fence != p.submissionFence.Get() || value != submitted->value)
        return p.Result(DlssError::InvalidFence);
    const auto error = p.CheckFence(fence, value);
    if (error != DlssError::None) {
        if (error != DlssError::GpuPending) p.Invalidate();
        return p.Result(error);
    }
    if (FAILED(p.device->GetDeviceRemovedReason())) {
        p.Invalidate();
        return p.Result(DlssError::DeviceRemoved);
    }
    if (submitted->generation == p.generation && p.status.requested == DlssRequested::Quality) {
        p.status.completedTicket = std::max(p.status.completedTicket, ticket);
        p.status.applied = DlssApplied::Quality;
        if (p.status.pendingTicket == ticket) p.status.pendingTicket = 0;
    }
    p.submissions.erase(submitted);
    return p.Result(DlssError::None);
#endif
}

DlssResult DlssSuperResolution::Shutdown(ID3D12Fence* fence, std::uint64_t value) {
    auto& p = *impl_;
#if !SR_HAS_DLSS
    (void)fence; (void)value;
#else
    if (p.gpuWorkRecorded) {
        const auto error = p.CheckFence(fence, value);
        if (error != DlssError::None) return p.Result(error);
        if (p.submissionFence && (fence != p.submissionFence.Get() || value < p.lastSubmittedValue))
            return p.Result(DlssError::InvalidFence);
    }
    // From here on, failures leave a teardown-only object, never a usable null map.
    p.teardownOnly = true;
    p.Invalidate();
    p.status.support = DlssSupport::NotQueried;
    if (p.feature) {
        const auto result = NVSDK_NGX_D3D12_ReleaseFeature(p.feature);
        if (NVSDK_NGX_FAILED(result)) return p.Result(DlssError::SdkFailure, result);
        p.feature = nullptr;
    }
    if (p.parameters) {
        const auto result = NVSDK_NGX_D3D12_DestroyParameters(p.parameters);
        if (NVSDK_NGX_FAILED(result)) return p.Result(DlssError::SdkFailure, result);
        p.parameters = nullptr;
    }
    if (p.initialized) {
        const auto result = NVSDK_NGX_D3D12_Shutdown1(p.device.Get());
        if (NVSDK_NGX_FAILED(result)) return p.Result(DlssError::SdkFailure, result);
    }
    p.device.Reset();
    p.applicationPath.clear();
    p.submissions.clear();
    p.submissionFence.Reset();
    p.lastSubmittedValue = 0;
    p.status.support = DlssSupport::NotQueried;
#endif
    p.initialized = p.gpuWorkRecorded = false;
    p.teardownOnly = false;
    p.unsubmittedTicket = 0;
    p.optimal = {};
    p.status.completedTicket = 0;
    p.Invalidate();
    return p.Result(DlssError::None);
}

const DlssStatus& DlssSuperResolution::GetStatus() const { return impl_->status; }

} // namespace superman_returns::graphics::d3d12
