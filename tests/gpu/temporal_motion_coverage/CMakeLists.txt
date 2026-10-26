cmake_minimum_required(VERSION 3.24)
project(sr_temporal_motion_coverage LANGUAGES CXX)
if(NOT WIN32)
  message(FATAL_ERROR "Requires Windows D3D12 and actual RTX 2060; no WARP/skips")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(PRODUCTION "${REPO}/port/src/graphics/d3d12")
set(DXC "${REPO}/.tools/vulkan-validation/Bin/dxc.exe" CACHE FILEPATH "Bundled DXC")
# Fixture HLSL is generated only in the standalone build directory.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/owner_fixture.hlsl" [=[
ByteAddressBuffer clips : register(t0);
cbuffer Draw : register(b0) {uint owner; uint checker;};
float4 OwnerVS(uint id:SV_VertexID):SV_Position {return asfloat(clips.Load4(id*16));}
uint OwnerPS(float4 p:SV_Position):SV_Target0 {
  if(checker && ((uint(p.x)/4+uint(p.y)/4)&1)) discard;
  return owner;
}
]=])
foreach(STAGE VS PS)
  string(TOLOWER "${STAGE}" PROFILE)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/owner_${STAGE}.h"
    COMMAND "${DXC}" -T "${PROFILE}_6_0" -E "Owner${STAGE}" -O3 -Gis -WX
      -Fh "${CMAKE_CURRENT_BINARY_DIR}/owner_${STAGE}.h" -Vn "kOwner${STAGE}"
      "${CMAKE_CURRENT_BINARY_DIR}/owner_fixture.hlsl"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/owner_fixture.hlsl" VERBATIM)
  list(APPEND FIXTURE_HEADERS "${CMAKE_CURRENT_BINARY_DIR}/owner_${STAGE}.h")
endforeach()
add_executable(temporal_motion_coverage_smoke smoke.cpp "${PRODUCTION}/temporal_motion.cpp" ${FIXTURE_HEADERS})
target_include_directories(temporal_motion_coverage_smoke PRIVATE "${PRODUCTION}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(temporal_motion_coverage_smoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(temporal_motion_coverage_smoke PRIVATE d3d12 dxgi dxguid)
if(MSVC)
  target_compile_options(temporal_motion_coverage_smoke PRIVATE /W4 /WX /EHsc)
endif()
enable_testing()
add_test(NAME temporal_motion_coverage_gpu COMMAND temporal_motion_coverage_smoke)
set_tests_properties(temporal_motion_coverage_gpu PROPERTIES TIMEOUT 120)
