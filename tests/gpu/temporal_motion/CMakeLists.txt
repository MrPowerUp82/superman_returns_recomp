cmake_minimum_required(VERSION 3.24)
project(sr_temporal_motion LANGUAGES CXX)
if(NOT WIN32)
  message(FATAL_ERROR "Temporal motion smoke requires Windows and NVIDIA hardware")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(MOTION_DIR "${REPO}/port/src/graphics/d3d12")
set(DXC "${REPO}/.tools/vulkan-validation/Bin/dxc.exe" CACHE FILEPATH "DXC compiler")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/fixture_vs.h"
  COMMAND "${DXC}" -T vs_6_0 -E DepthVS -O3 -WX
    -Fh "${CMAKE_CURRENT_BINARY_DIR}/fixture_vs.h" -Vn kFixtureVS
    "${CMAKE_CURRENT_SOURCE_DIR}/fixture.hlsl"
  DEPENDS fixture.hlsl VERBATIM)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/fixture_ps.h"
  COMMAND "${DXC}" -T ps_6_0 -E CoveragePS -O3 -WX
    -Fh "${CMAKE_CURRENT_BINARY_DIR}/fixture_ps.h" -Vn kFixturePS
    "${CMAKE_CURRENT_SOURCE_DIR}/fixture.hlsl"
  DEPENDS fixture.hlsl VERBATIM)
foreach(STAGE VS PS)
  string(TOLOWER "${STAGE}" PROFILE)
  set(HEADER "${MOTION_DIR}/shaders/temporal_motion_${STAGE}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${DXC}" -T ${PROFILE}_6_0 -E Motion${STAGE} -O3 -Gis -WX
      -Fh "${HEADER}" -Vn kTemporalMotion${STAGE}
      "${MOTION_DIR}/shaders/temporal_motion.hlsl"
    DEPENDS "${MOTION_DIR}/shaders/temporal_motion.hlsl" VERBATIM)
  list(APPEND MOTION_HEADERS "${HEADER}")
endforeach()
add_executable(temporal_motion_smoke smoke.cpp "${MOTION_DIR}/temporal_motion.cpp"
  "${CMAKE_CURRENT_BINARY_DIR}/fixture_vs.h" "${CMAKE_CURRENT_BINARY_DIR}/fixture_ps.h" ${MOTION_HEADERS})
target_include_directories(temporal_motion_smoke PRIVATE "${MOTION_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(temporal_motion_smoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(temporal_motion_smoke PRIVATE d3d12 dxgi dxguid)
if(MSVC)
  target_compile_options(temporal_motion_smoke PRIVATE /W4 /WX /EHsc)
endif()
enable_testing()
add_test(NAME temporal_motion_gpu COMMAND temporal_motion_smoke)
set_tests_properties(temporal_motion_gpu PROPERTIES TIMEOUT 120)

# Independent sidecar: building this target never builds temporal_motion_smoke
# or regenerates its production shader headers. Keep all generated files local.
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/static_world_address_cs.h"
    "${CMAKE_CURRENT_BINARY_DIR}/static_world_address_cs.ll"
  COMMAND "${DXC}" -T cs_6_0 -E AddressCS -O3 -Gis -WX
    -Fh "${CMAKE_CURRENT_BINARY_DIR}/static_world_address_cs.h" -Vn kStaticWorldAddressCS
    -Fc "${CMAKE_CURRENT_BINARY_DIR}/static_world_address_cs.ll"
    "${CMAKE_CURRENT_SOURCE_DIR}/static_world_address.hlsl"
  DEPENDS static_world_address.hlsl VERBATIM)
add_executable(static_world_address_smoke static_world_address_smoke.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/static_world_address_cs.h"
  "${CMAKE_CURRENT_BINARY_DIR}/static_world_address_cs.ll")
target_include_directories(static_world_address_smoke PRIVATE "${REPO}/port/src/graphics"
  "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(static_world_address_smoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(static_world_address_smoke PRIVATE d3d12 dxgi dxguid)
if(MSVC)
  target_compile_options(static_world_address_smoke PRIVATE /W4 /WX /EHsc)
endif()
add_test(NAME static_world_address_gpu COMMAND static_world_address_smoke
  "${CMAKE_CURRENT_BINARY_DIR}/static_world_address_cs.ll")
set_tests_properties(static_world_address_gpu PROPERTIES TIMEOUT 120)
