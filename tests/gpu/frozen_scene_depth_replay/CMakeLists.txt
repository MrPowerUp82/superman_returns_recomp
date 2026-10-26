cmake_minimum_required(VERSION 3.24)
project(sr_frozen_scene_depth_replay LANGUAGES CXX)
if(NOT WIN32)
  message(FATAL_ERROR "Requires Windows D3D12 and an actual RTX 2060; no WARP fallback")
endif()
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_CURRENT_BINARY_DIR)
  message(FATAL_ERROR "Build in a separate directory, preferably LOCALAPPDATA")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(DXC "${REPO}/.tools/dxc/bin/x64/dxc.exe" CACHE FILEPATH "Existing DXC compiler")
if(NOT EXISTS "${DXC}")
  message(FATAL_ERROR "Missing DXC: ${DXC}")
endif()
# Only these two source files belong to this sidecar. Shaders and headers are
# generated in its private build directory; production headers are never touched.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/frozen_replay.hlsl" [=[
ByteAddressBuffer clips : register(t0);
cbuffer Draw : register(b0) { uint firstVertex; uint category; };
struct Vertex { precise float4 position : SV_Position; };
Vertex ClipVS(uint id : SV_VertexID) {
  uint triangleBase = firstVertex + (id / 3) * 3;
  float4 a = asfloat(clips.Load4(triangleBase * 16));
  float4 b = asfloat(clips.Load4((triangleBase + 1) * 16));
  float4 c = asfloat(clips.Load4((triangleBase + 2) * 16));
  bool good = all(isfinite(a)) && all(isfinite(b)) && all(isfinite(c)) &&
              a.w != 0 && b.w != 0 && c.w != 0;
  Vertex result;
  result.position = good ? asfloat(clips.Load4((firstVertex + id) * 16)) : float4(0,0,-1,1);
  return result;
}
uint CoveragePS() : SV_Target { return category; }
]=])
foreach(STAGE VS PS)
  if(STAGE STREQUAL VS)
    set(ENTRY ClipVS)
    set(PROFILE vs_6_0)
  else()
    set(ENTRY CoveragePS)
    set(PROFILE ps_6_0)
  endif()
  set(HEADER "${CMAKE_CURRENT_BINARY_DIR}/frozen_${STAGE}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${DXC}" -T "${PROFILE}" -E "${ENTRY}" -O3 -Gis -WX
      -Fh "${HEADER}" -Vn "kFrozen${STAGE}" "${CMAKE_CURRENT_BINARY_DIR}/frozen_replay.hlsl"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/frozen_replay.hlsl" VERBATIM)
  list(APPEND HEADERS "${HEADER}")
endforeach()
add_executable(frozen_scene_depth_replay smoke.cpp ${HEADERS})
target_include_directories(frozen_scene_depth_replay PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(frozen_scene_depth_replay PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(frozen_scene_depth_replay PRIVATE d3d12 dxgi dxguid)
if(MSVC)
  target_compile_options(frozen_scene_depth_replay PRIVATE /W4 /WX /EHsc)
endif()
enable_testing()
add_test(NAME frozen_scene_depth_controls COMMAND frozen_scene_depth_replay --self-test)
set_tests_properties(frozen_scene_depth_controls PROPERTIES TIMEOUT 120)
