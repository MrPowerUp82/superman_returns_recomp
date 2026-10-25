#include "test_main.h"
#include "../../port/src/graphics/graphics_options.h"
#include <thread>

using namespace superman_returns::graphics;
namespace {
constexpr auto Shadows = GraphicsOption::kRtShadows;
constexpr auto Dlss = GraphicsOption::kDlss;
constexpr size_t S = static_cast<size_t>(Shadows);
constexpr size_t D = static_cast<size_t>(Dlss);
GraphicsBackendReport Ready(uint64_t revision = 0, uint64_t frame = 1) {
  GraphicsBackendReport report;
  report.request_revision = revision;
  report.frame = frame;
  for (auto& feature : report.features) feature = {true, true, ""};
  return report;
}
}

SR_TEST(graphics_options_inactive_softness_preference_is_not_a_failed_request) {
  GraphicsOptions options;
  const auto epoch=options.BeginBackend(NativeApi::kD3D12);
  GraphicsBackendReport off;
  off.frame=1;
  SR_CHECK(options.PublishBackend(epoch,off));
  SR_CHECK(options.Snapshot().failure.empty());
  SR_CHECK_EQ(options.Snapshot().requested[1],1);
  SR_CHECK_EQ(options.Snapshot().applied[1],0);
}

SR_TEST(graphics_options_never_confuses_requested_capable_and_applied) {
  GraphicsOptions options;
  SR_CHECK(!options.Request(Shadows, 1));
  const auto epoch = options.BeginBackend(NativeApi::kD3D12);
  SR_CHECK(options.PublishBackend(epoch, Ready()));
  SR_CHECK(options.Request(Shadows, 1));
  auto view = options.Snapshot();
  SR_CHECK_EQ(view.requested[S], 1);
  SR_CHECK_EQ(view.applied[S], 0);
  SR_CHECK(view.pending());
  SR_CHECK(GraphicsValueLabel(Shadows, view.applied[S]) == "Off");
  auto report = Ready(view.request_revision, 2);
  report.applied[S] = 1;
  SR_CHECK(options.PublishBackend(epoch, report));
  view = options.Snapshot();
  SR_CHECK(!view.pending());
  SR_CHECK(GraphicsValueLabel(Shadows, view.applied[S]) == "On");
  SR_CHECK(options.Request(Shadows, 0));
  SR_CHECK_EQ(options.Snapshot().applied[S], 1); // Off request is not an acknowledgement.
}

SR_TEST(graphics_options_rejects_stale_backend_frames_and_future_acknowledgements) {
  GraphicsOptions options;
  auto old_epoch = options.BeginBackend(NativeApi::kD3D12);
  options.PublishBackend(old_epoch, Ready());
  options.Request(Shadows, 1);
  auto report = Ready(1, 3);
  report.applied[S] = 1;
  SR_CHECK(options.PublishBackend(old_epoch, report));
  SR_CHECK(!options.PublishBackend(old_epoch, Ready(0, 2)));
  SR_CHECK(!options.PublishBackend(old_epoch, Ready(0, 4)));
  SR_CHECK(!options.PublishBackend(old_epoch, Ready(2, 4)));
  const auto epoch = options.BeginBackend(NativeApi::kVulkan);
  SR_CHECK(!options.PublishBackend(old_epoch, report));
  const auto view = options.Snapshot();
  SR_CHECK_EQ(view.requested[S], 1);
  SR_CHECK_EQ(view.applied[S], 0);
  SR_CHECK(!view.features[S].capable);
  SR_CHECK(view.pending());
  SR_CHECK(options.PublishBackend(epoch, Ready(1, 1)));
}

SR_TEST(graphics_options_rejects_false_on_and_reports_failed_apply) {
  GraphicsOptions options;
  auto epoch = options.BeginBackend(NativeApi::kD3D12);
  auto report = Ready();
  report.features[S].implemented = false;
  SR_CHECK(options.PublishBackend(epoch, report));
  SR_CHECK(!options.Request(Shadows, 1));
  report.frame = 2;
  report.applied[S] = 1;
  SR_CHECK(!options.PublishBackend(epoch, report));
  SR_CHECK_EQ(options.Snapshot().applied[S], 0);
  report = Ready(0, 3);
  options.PublishBackend(epoch, report);
  options.Request(Dlss, 1);
  report = Ready(1, 4);
  report.failure = "DLSS evaluate failed";
  SR_CHECK(options.PublishBackend(epoch, report));
  SR_CHECK(!options.Snapshot().pending());
  SR_CHECK_EQ(options.Snapshot().applied[D], 0);
  SR_CHECK(!options.Snapshot().failure.empty());
  SR_CHECK(!options.Request(Dlss, 2));
  SR_CHECK(!options.Request(static_cast<GraphicsOption>(99), 1));
}

SR_TEST(graphics_options_snapshot_and_per_option_requests_preserve_concurrent_changes) {
  GraphicsOptions options;
  const auto epoch = options.BeginBackend(NativeApi::kD3D12);
  options.PublishBackend(epoch, Ready());
  std::thread first([&] { for (int i=0;i<1000;++i) options.Request(Shadows, i%2); options.Request(Shadows,1); });
  std::thread second([&] { for (int i=0;i<1000;++i) options.Request(Dlss, i%2); options.Request(Dlss,1); });
  for (int i=0;i<1000;++i) {
    const auto view = options.Snapshot();
    SR_CHECK(view.completed_revision <= view.request_revision);
  }
  first.join(); second.join();
  SR_CHECK_EQ(options.Snapshot().requested[S], 1);
  SR_CHECK_EQ(options.Snapshot().requested[D], 1);
}

SR_TEST(graphics_options_older_completed_request_keeps_newer_ui_edits_pending) {
  GraphicsOptions options;
  const auto epoch = options.BeginBackend(NativeApi::kD3D12);
  options.PublishBackend(epoch,Ready());
  options.Request(Shadows,1);
  const auto renderer_request = options.Snapshot();
  options.Request(Dlss,1);
  auto report = Ready(renderer_request.request_revision,2);
  report.applied[S] = 1;
  SR_CHECK(options.PublishBackend(epoch,report));
  const auto view = options.Snapshot();
  SR_CHECK(view.pending());
  SR_CHECK_EQ(view.requested[D],1);
  SR_CHECK_EQ(view.applied[D],0);
  SR_CHECK_EQ(view.applied[S],1);
  SR_CHECK_EQ(view.completed_revision,1u);
}

SR_TEST(graphics_options_device_loss_clears_effective_state_without_losing_request) {
  GraphicsOptions options;
  auto epoch = options.BeginBackend(NativeApi::kD3D12);
  options.PublishBackend(epoch,Ready());
  options.Request(Shadows,1);
  auto report = Ready(1,2); report.applied[S]=1;
  options.PublishBackend(epoch,report);
  report.frame=3; report.features[S].capable=false;
  report.features[S].reason="Device lost";
  SR_CHECK(!options.PublishBackend(epoch,report)); // Cannot keep On with lost capability.
  epoch = options.BeginBackend(NativeApi::kD3D12);
  SR_CHECK_EQ(options.Snapshot().applied[S],0);
  SR_CHECK_EQ(options.Snapshot().requested[S],1);
  SR_CHECK(options.Snapshot().pending());
  SR_CHECK(options.Request(Shadows,0)); // Off always available, even without a device.
  SR_CHECK(!options.PublishBackend(epoch,Ready(99,1)));
}
