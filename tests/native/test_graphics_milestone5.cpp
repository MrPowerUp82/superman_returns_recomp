#include "test_main.h"
#include "../../port/src/render_scale_policy.h"
#include "../../port/src/graphics/graphics_preferences.h"
using namespace superman_returns::graphics;

SR_TEST(graphics_m5_dlss_modes_and_api_restart_are_requests_only) {
  GraphicsOptions options;
  SR_CHECK(options.Request(GraphicsOption::kApi,1));
  SR_CHECK_EQ(options.Snapshot().requested[5],1);
  SR_CHECK_EQ(options.Snapshot().applied[5],0);
  const auto epoch=options.BeginBackend(NativeApi::kD3D12);
  GraphicsBackendReport report;report.frame=1;report.request_revision=options.Snapshot().request_revision;
  report.features[4]={true,true,{}};
  SR_CHECK(options.PublishBackend(epoch,report));
  for(int mode=0;mode<=4;++mode) SR_CHECK(options.Request(GraphicsOption::kDlss,mode));
  SR_CHECK(!options.Request(GraphicsOption::kDlss,5));
  SR_CHECK_EQ(options.Snapshot().applied[4],0);
}
SR_TEST(graphics_m5_partial_ini_preserves_defaults_and_validates_each_field) {
  GraphicsValues values{0,1,0,0,0,0,1,0,0,0,2};
  const auto present=ReadLauncherGraphicsPreferences("dlss=4\nresolution=3840x2160\nfps_limit=120\nrender_scale=bogus\nunknown=abc\n",values);
  SR_CHECK(present[4] && present[6] && present[7]);SR_CHECK(!present[10]);
  SR_CHECK_EQ(values[4],4);SR_CHECK_EQ(values[6],5);SR_CHECK_EQ(values[7],3);SR_CHECK_EQ(values[10],2);
}
SR_TEST(graphics_m5_merge_keeps_unrelated_and_external_field_edits) {
  const GraphicsValues previous{0,1,0,0,0,0,1,0,0,0,2};
  auto requested=previous;requested[4]=3;
  const std::string disk="# keep me\nlanguage=pt-BR\nfuture=abc\nfps_limit=120\ndlss=1\ndlss=2\n";
  const auto merged=MergeLauncherGraphicsPreferences(disk,previous,requested);
  SR_CHECK(merged.find("# keep me\nlanguage=pt-BR\nfuture=abc\nfps_limit=120\n")!=std::string::npos);
  SR_CHECK(merged.find("dlss=3\n")!=std::string::npos);
  SR_CHECK(merged.find("dlss=2\n")==std::string::npos);
  auto parsed=previous;ReadLauncherGraphicsPreferences(merged,parsed);
  SR_CHECK_EQ(parsed[7],3);SR_CHECK_EQ(parsed[4],3);
}
SR_TEST(graphics_m5_legacy_migration_retains_old_intent_and_fills_only_missing_keys) {
  GraphicsValues values{};
  SR_CHECK(ParseGraphicsPreferences("version=1\nrt_shadows=1\nshadow_softness=2\ncharacter_shadows=1\nrt_reflections=1\ndlss=1\n",values));
  SR_CHECK_EQ(values[4],1);SR_CHECK_EQ(values[10],2);
  const auto merged=MergeLauncherGraphicsPreferences("dlss=4\nfuture=value\n",values,values,true);
  auto parsed=values;ReadLauncherGraphicsPreferences(merged,parsed);
  SR_CHECK_EQ(parsed[4],4);SR_CHECK_EQ(parsed[0],1);
  SR_CHECK(merged.find("future=value\n")!=std::string::npos);
}

SR_TEST(graphics_m5_dlss_auto_scale_keeps_uniform_coordinates_and_rejects_unsupported_range) {
  const auto quality=ComputeGraphicsRenderExtent(1280,720,1,640,360,1280,720);
  SR_CHECK(quality.in_range);SR_CHECK_EQ(quality.width,1280u);SR_CHECK_EQ(quality.height,720u);
  const auto balanced=ComputeGraphicsRenderExtent(1114,626,1,640,360,1280,720);
  SR_CHECK(balanced.in_range);SR_CHECK_EQ(balanced.height,626u);
  SR_CHECK(balanced.width<=1114 && balanced.width>=1112);
  const auto supersample=ComputeGraphicsRenderExtent(1280,720,2,640,360,1280,720);
  SR_CHECK(!supersample.in_range);
  const auto off=ComputeGraphicsRenderExtent(1920,1080,.5f,1,1,16384,16384);
  SR_CHECK(off.in_range);SR_CHECK_EQ(off.width,960u);SR_CHECK_EQ(off.height,540u);
  SR_CHECK(!ComputeGraphicsRenderExtent(0,720,1,1,1,16384,16384).in_range);
}

SR_TEST(graphics_m5_backend_cannot_claim_unimplemented_resolution_was_applied) {
  GraphicsOptions options;
  const auto epoch=options.BeginBackend(NativeApi::kD3D12);
  GraphicsBackendReport report;report.frame=1;report.applied[6]=3;
  SR_CHECK(!options.PublishBackend(epoch,report));
  SR_CHECK(!options.Snapshot().features[6].implemented);
  SR_CHECK_EQ(options.Snapshot().applied[6],0);
  report.features[6]={true,true,{}};
  SR_CHECK(options.PublishBackend(epoch,report));
  SR_CHECK_EQ(options.Snapshot().applied[6],3);
  SR_CHECK_EQ(options.Snapshot().requested[6],1);
}
SR_TEST(graphics_m5_every_requested_selector_survives_canonical_ini_roundtrip) {
  const GraphicsValues values{1,2,1,1,4,1,5,3,1,2,7};
  const auto ini=MergeLauncherGraphicsPreferences("future=keep\n",values,values,true);
  GraphicsValues restored{};
  const auto present=ReadLauncherGraphicsPreferences(ini,restored);
  for(bool field:present) SR_CHECK(field);
  SR_CHECK(restored==values);
  SR_CHECK(ini.find("future=keep\n")!=std::string::npos);
}

SR_TEST(graphics_m5_fractional_viewport_edges_match_integer_targets_and_neighbor_cells) {
  namespace rs=superman_returns::render_scale;
  for(float scale:{853.f/1280.f,1113.f/1280.f,.5f,1.f,1.25f,2.f}) {
    const auto full=rs::HostViewportAxis(0,720,scale);
    SR_CHECK_EQ(full.first,0.f);SR_CHECK_EQ(full.second,float(rs::HostDimension(720,scale)));
    const auto left=rs::HostViewportAxis(-10,650,scale),right=rs::HostViewportAxis(640,640,scale);
    SR_CHECK_EQ(left.first+left.second,right.first);
    SR_CHECK_EQ(right.first+right.second,float(rs::HostDimension(1280,scale)));
  }
  SR_CHECK_EQ(rs::HostViewportAxis(0,720,853.f/1280.f).second,480.f);
}
