#include "../../port/src/graphics/scene_raster_jitter.h"
#include "test_main.h"
#include <cmath>

using namespace superman_returns::graphics;

SR_TEST(RasterJitter_ExpectedSamples) {
    SceneRasterJitter j0, j1, j7;
    SR_CHECK(MakeSceneRasterJitter(0, 1920, 1080, true, j0));
    SR_CHECK(std::abs(j0.pixel_x - 0.0f) < 1e-6f && std::abs(j0.pixel_y - (-1.0f / 6.0f)) < 1e-6f);
    SR_CHECK(MakeSceneRasterJitter(1, 1920, 1080, true, j1));
    SR_CHECK(std::abs(j1.pixel_x - (-0.25f)) < 1e-6f && std::abs(j1.pixel_y - (1.0f / 6.0f)) < 1e-6f);
    SR_CHECK(MakeSceneRasterJitter(7, 1920, 1080, true, j7));
    SR_CHECK(std::abs(j7.pixel_x - (-7.0f / 16.0f)) < 1e-6f && std::abs(j7.pixel_y - (7.0f / 18.0f)) < 1e-6f);
}

SR_TEST(RasterJitter_CycleAndUint64Max) {
    SceneRasterJitter a, b, c;
    SR_CHECK(MakeSceneRasterJitter(0, 800, 600, true, a));
    SR_CHECK(MakeSceneRasterJitter(8, 800, 600, true, b));
    SR_CHECK(a==b);
    SR_CHECK(MakeSceneRasterJitter(UINT64_MAX, 800, 600, true, c));
    SceneRasterJitter d;
    SR_CHECK(MakeSceneRasterJitter(7,800,600,true,d));
    SR_CHECK(c==d);
}

SR_TEST(RasterJitter_Bounds) {
    SceneRasterJitter j;
    for (uint64_t i = 0; i < 8; ++i) {
        SR_CHECK(MakeSceneRasterJitter(i, 1280, 720, true, j));
        SR_CHECK(j.pixel_x >= -0.5f && j.pixel_x < 0.5f && j.pixel_y >= -0.5f && j.pixel_y < 0.5f);
    }
}

SR_TEST(RasterJitter_FloatNormalization) {
    SceneRasterJitter j;
    SR_CHECK(MakeSceneRasterJitter(1, 1920, 1080, true, j));
    SR_CHECK(std::abs(j.ndc_x*960.0f - (-0.25f)) < 1e-6f);
    SR_CHECK(std::abs(j.ndc_y*(-540.0f) - (1.0f/6.0f)) < 1e-6f);
}

SR_TEST(RasterJitter_InvalidExtentsPreserveOutput) {
    SceneRasterJitter sent{9.f, 9.f, 9.f, 9.f};
    SR_CHECK(!MakeSceneRasterJitter(0, 0, 1080, true, sent));
    SR_CHECK((sent==SceneRasterJitter{9.f,9.f,9.f,9.f}));
    SR_CHECK(!MakeSceneRasterJitter(0, 8193, 1080, false, sent));
    SR_CHECK((sent==SceneRasterJitter{9.f,9.f,9.f,9.f}));
}

SR_TEST(RasterJitter_DisabledZero) {
    SceneRasterJitter j{1.f, 2.f, 3.f, 4.f};
    SR_CHECK(MakeSceneRasterJitter(3, 1920, 1080, false, j));
    SR_CHECK(j==SceneRasterJitter{});
}

SR_TEST(RasterJitter_AreaBudget) {
    SceneRasterJitter j;
    SR_CHECK(MakeSceneRasterJitter(0, 4096, 4096, true, j));
    SceneRasterJitter sent{1.f, 1.f, 1.f, 1.f};
    SR_CHECK(!MakeSceneRasterJitter(0, 4097, 4096, true, sent));
    SR_CHECK_EQ(sent.pixel_x, 1.f);
}
