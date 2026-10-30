// Standalone: compile with dlss_super_resolution.cpp, SR_HAS_DLSS=0. No SDK/GPU.
#include "../../port/src/graphics/d3d12/dlss_super_resolution.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace superman_returns::graphics::d3d12;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); std::abort(); } } while (false)

int main() {
    // Removing a temporal guard must admit these malformed frames and fail here.
    DlssFrameContract f{};
    CHECK(ValidateDlssFrame(f) == DlssError::InvalidDimensions);
    f.renderWidth = 1280; f.renderHeight = 720;
    f.outputWidth = 1920; f.outputHeight = 1080;
    CHECK(ValidateDlssFrame(f) == DlssError::UnverifiedTemporalInputs);
    f.conventionsVerified = true;
    f.colorBeforeHud = true;
    f.rasterJitterApplied = true;
    f.motionIncludesCameraObjectsAndDeformation = true;
    f.depth = DlssDepth::Forward;
    f.motion = DlssMotion::CurrentToPreviousPixels;
    f.mvScaleX = 1; f.mvScaleY = 1;
    CHECK(ValidateDlssFrame(f) == DlssError::None);
    f.motionIncludesCameraObjectsAndDeformation = false;
    CHECK(ValidateDlssFrame(f) == DlssError::UnverifiedTemporalInputs);
    f.motionCameraOnlyAccepted = true; // Explicit choice, not a claim of full motion.
    CHECK(ValidateDlssFrame(f) == DlssError::None);
    f.motionCameraOnlyAccepted = false;
    f.motionIncludesCameraObjectsAndDeformation = true;
    f.depth = DlssDepth::Unknown;
    CHECK(ValidateDlssFrame(f) == DlssError::UnsupportedConventions);
    f.depth = DlssDepth::Reversed;
    f.motion = DlssMotion::Unknown;
    CHECK(ValidateDlssFrame(f) == DlssError::UnsupportedConventions);
    f.motion = DlssMotion::CurrentToPreviousPixels;
    f.jitterX = std::numeric_limits<float>::quiet_NaN();
    CHECK(ValidateDlssFrame(f) == DlssError::InvalidScalars);
    f.jitterX = 0; f.mvScaleY = 0;
    CHECK(ValidateDlssFrame(f) == DlssError::InvalidScalars);
    f.mvScaleY = -720; // normalized Y-up velocity can be converted explicitly.
    CHECK(ValidateDlssFrame(f) == DlssError::None);
    f.preExposure = 0;
    CHECK(ValidateDlssFrame(f) == DlssError::InvalidScalars);
    f.preExposure = 1; f.renderWidth = 1920; f.renderHeight = 1080;
    CHECK(ValidateDlssFrame(f) == DlssError::InvalidDimensions);

    // SDK absence must never advertise support, produce a ticket, or apply Quality.
    DlssSuperResolution dlss;
    dlss.SetRequested(DlssRequested::Quality);
    CHECK(dlss.Initialize(nullptr, L"").error == DlssError::SdkUnavailable);
    CHECK(dlss.GetStatus().support == DlssSupport::SdkUnavailable);
    CHECK(dlss.GetStatus().requested == DlssRequested::Quality);
    CHECK(dlss.GetStatus().applied == DlssApplied::Off);
    DlssOptimalSettings optimal{};
    CHECK(dlss.QueryOptimalSettings(1920, 1080, optimal).error == DlssError::SdkUnavailable);
    DlssEvaluation e{};
    CHECK(dlss.Evaluate(e).ticket == 0);
    CHECK(dlss.NotifySubmitted(1, nullptr, 1).error == DlssError::SdkUnavailable);
    CHECK(dlss.ConfirmExecution(1, nullptr, 1).error != DlssError::None);
    CHECK(dlss.GetStatus().applied == DlssApplied::Off);
    dlss.SetRequested(DlssRequested::Off);
    CHECK(dlss.GetStatus().requested == DlssRequested::Off);
    CHECK(dlss.Shutdown().error == DlssError::None);
    std::puts("DLSS contract and SDK-absent tests passed");
}
