cmake_minimum_required(VERSION 3.24)
project(sr_rt_smoke LANGUAGES CXX)
if(NOT WIN32)
  message(FATAL_ERROR "DXR smoke requires Windows and a hardware NVIDIA DXR 1.1 adapter")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(RT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../port/src/graphics/d3d12")
set(DXC "${CMAKE_CURRENT_SOURCE_DIR}/../../.tools/vulkan-validation/Bin/dxc.exe" CACHE FILEPATH "DXC shader compiler")
foreach(ENTRY TraceRays TraceScene)
  set(HEADER "${RT_DIR}/shaders/rt_scene_${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${DXC}" -T cs_6_5 -E ${ENTRY} -O3 -WX
      -Fh "${HEADER}" -Vn rt_scene_${ENTRY} "${RT_DIR}/shaders/rt_scene.hlsl"
    DEPENDS "${RT_DIR}/shaders/rt_scene.hlsl" VERBATIM)
  list(APPEND RT_HEADERS "${HEADER}")
endforeach()
add_custom_command(OUTPUT "${RT_DIR}/shaders/depth_receivers.h"
  COMMAND "${DXC}" -T cs_6_5 -E ProbeDepth -O3 -WX
    -Fh "${RT_DIR}/shaders/depth_receivers.h" -Vn kDepthReceivers "${RT_DIR}/shaders/depth_receivers.hlsl"
  DEPENDS "${RT_DIR}/shaders/depth_receivers.hlsl" VERBATIM)
list(APPEND RT_HEADERS "${RT_DIR}/shaders/depth_receivers.h")
add_custom_command(OUTPUT "${RT_DIR}/shaders/post_vertex_clip_gs.h"
  COMMAND "${DXC}" -T gs_6_0 -E ExtractClip -O3 -WX
    -Fh "${RT_DIR}/shaders/post_vertex_clip_gs.h" -Vn kPostVertexClipGS "${RT_DIR}/shaders/post_vertex_clip.hlsl"
  DEPENDS "${RT_DIR}/shaders/post_vertex_clip.hlsl" VERBATIM)
list(APPEND POST_VS_HEADERS "${RT_DIR}/shaders/post_vertex_clip_gs.h")
foreach(WORLD 0 1 2)
  add_custom_command(OUTPUT "${RT_DIR}/shaders/post_vertex_world${WORLD}_gs.h"
    COMMAND "${DXC}" -T gs_6_0 -E ExtractTriangle -DWORLD_TEXCOORD=${WORLD} -O3 -WX
      -Fh "${RT_DIR}/shaders/post_vertex_world${WORLD}_gs.h" -Vn kPostVertexWorld${WORLD}GS "${RT_DIR}/shaders/post_vertex.hlsl"
    DEPENDS "${RT_DIR}/shaders/post_vertex.hlsl" VERBATIM)
  list(APPEND POST_VS_HEADERS "${RT_DIR}/shaders/post_vertex_world${WORLD}_gs.h")
endforeach()
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shadow_export_fixture.h"
  COMMAND "${DXC}" -T vs_6_0 -E ExportCaster -O3 -WX
    -Fh "${CMAKE_CURRENT_BINARY_DIR}/shadow_export_fixture.h" -Vn kShadowExportFixture "${CMAKE_CURRENT_SOURCE_DIR}/shadow_export_fixture.hlsl"
  DEPENDS shadow_export_fixture.hlsl VERBATIM)
add_executable(rt_scene_smoke rt_scene_smoke.cpp "${RT_DIR}/ray_tracing_scene.cpp" "${RT_DIR}/post_vertex_clip_capture.cpp")
target_sources(rt_scene_smoke PRIVATE ${RT_HEADERS} ${POST_VS_HEADERS} "${CMAKE_CURRENT_BINARY_DIR}/shadow_export_fixture.h")
target_include_directories(rt_scene_smoke PRIVATE "${RT_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(rt_scene_smoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(rt_scene_smoke PRIVATE d3d12 dxgi dxguid)
if(MSVC)
  target_compile_options(rt_scene_smoke PRIVATE /W4 /EHsc)
endif()
enable_testing()
add_executable(post_vertex_pipeline_smoke post_vertex_pipeline_smoke.cpp)
target_sources(post_vertex_pipeline_smoke PRIVATE ${POST_VS_HEADERS})
target_link_libraries(post_vertex_pipeline_smoke PRIVATE d3d12 dxgi dxguid)
add_test(NAME post_vertex_pipeline_gpu COMMAND post_vertex_pipeline_smoke "${CMAKE_CURRENT_SOURCE_DIR}/../../artifacts/shaders/dxil")
add_test(NAME rt_scene_gpu COMMAND rt_scene_smoke)
set_tests_properties(rt_scene_gpu PROPERTIES TIMEOUT 60)
