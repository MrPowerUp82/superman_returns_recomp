#include "test_main.h"
#include "../../port/src/graphics/graphics_preferences.h"
using namespace superman_returns::graphics;

SR_TEST(graphics_preferences_roundtrip_preserves_requested_not_applied) {
  const GraphicsValues values{1,2,1,1,1};
  GraphicsValues parsed{};
  SR_CHECK(ParseGraphicsPreferences(SerializeGraphicsPreferences(values),parsed));
  SR_CHECK(parsed==values);
  GraphicsOptions options;
  GraphicsPreferencesBridge bridge;
  SR_CHECK(bridge.Initialize(options,parsed));
  SR_CHECK(options.Snapshot().requested==values);
  SR_CHECK(options.Snapshot().applied==GraphicsValues{});
  SR_CHECK(!options.Snapshot().features[0].implemented);
  SR_CHECK(!options.Request(GraphicsOption::kRtShadows,1));
}
SR_TEST(graphics_preferences_rejects_corruption_without_partial_restore) {
  const GraphicsValues original{0,1,0,0,0};
  for(const auto text:{"version=9\n", "version=1\nrt_shadows=2\n",
      "version=1\nshadow_softness=0\n", "version=1\nrt_shadows=1\nrt_shadows=0\n",
      "version=1\nrt_reflections=1junk\n", "version=1\ndlss=1\n"}) {
    auto values=original;
    SR_CHECK(!ParseGraphicsPreferences(text,values));
    SR_CHECK(values==original);
  }
}
SR_TEST(graphics_preferences_merges_external_and_menu_fields_and_rejects_unavailable_on) {
  GraphicsOptions options;
  GraphicsPreferencesBridge bridge;
  const GraphicsValues defaults{0,1,0,0,0};
  SR_CHECK(bridge.Initialize(options,defaults));
  auto epoch=options.BeginBackend(NativeApi::kD3D12);
  GraphicsBackendReport report; report.frame=1;
  for(auto& feature:report.features) feature={true,true,{}};
  SR_CHECK(options.PublishBackend(epoch,report));
  SR_CHECK(options.Request(GraphicsOption::kRtShadows,1)); // Menu change.
  auto external=defaults; external[4]=1; // Simultaneous launcher/cvar change.
  auto merged=bridge.Synchronize(options,external);
  SR_CHECK_EQ(merged[0],1); SR_CHECK_EQ(merged[4],1);
  SR_CHECK(bridge.Synchronize(options,merged)==merged);
  // External Off of one field must not overwrite another menu edit.
  SR_CHECK(options.Request(GraphicsOption::kCharacterShadows,1));
  external=merged; external[0]=0;
  merged=bridge.Synchronize(options,external);
  SR_CHECK_EQ(merged[0],0); SR_CHECK_EQ(merged[2],1);
  options.BeginBackend(NativeApi::kVulkan);
  external=merged; external[0]=1;
  SR_CHECK_EQ(bridge.Synchronize(options,external)[0],0);
}
