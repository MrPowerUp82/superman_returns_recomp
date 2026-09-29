# superman_returns - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(superman_returns LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(SUPERMANRETURNS_SOURCES
    src/main.cpp
    src/frame_stats.cpp
    src/xma_fixes.cpp
    src/sr_settings.cpp
    src/skip_intro.cpp
    src/renderdoc_capture.cpp
)

if(WIN32)
    add_executable(superman_returns WIN32 ${SUPERMANRETURNS_SOURCES})
else()
    add_executable(superman_returns ${SUPERMANRETURNS_SOURCES})
endif()

rexglue_setup_target(superman_returns GPU_PLUGINS xenos)

# Compile the SDK's D3D12 GPU source in-process so a project-owned command
# processor can observe each draw. The normal executable remains available
# without the optional source checkout and keeps using the Xenos plugin.
set(SR_REXGPU_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.tools/rexglue-sdk-source"
    CACHE PATH "ReXGlue v0.10.0 source checkout for the trace graphics system")
if(WIN32 AND EXISTS "${SR_REXGPU_SOURCE_DIR}/src/graphics/d3d12/command_processor.cpp")
    execute_process(
        COMMAND git -C "${SR_REXGPU_SOURCE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE SR_REXGPU_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE SR_REXGPU_GIT_RESULT
    )
    if(NOT SR_REXGPU_GIT_RESULT EQUAL 0 OR
       NOT SR_REXGPU_REVISION STREQUAL "f5337cdc947ff6d4c4196737e2c807a48f2a1fc2")
        message(FATAL_ERROR "Trace GPU sources must be ReXGlue v0.10.0 (f5337cd). Run tools/setup_gpu_source.ps1")
    endif()
    set(SR_GPU_ROOT "${SR_REXGPU_SOURCE_DIR}/src/graphics")
    set(SR_GPU_SOURCES
        ${SR_GPU_ROOT}/flags.cpp
        ${SR_GPU_ROOT}/register_file.cpp
        ${SR_GPU_ROOT}/registers.cpp
        ${SR_GPU_ROOT}/pipeline/shader/shader.cpp
        ${SR_GPU_ROOT}/format/ucode.cpp
        ${SR_GPU_ROOT}/sampler_info.cpp
        ${SR_GPU_ROOT}/graphics_system.cpp
        ${SR_GPU_ROOT}/command_processor.cpp
        ${SR_GPU_ROOT}/util/draw_extent_estimator.cpp
        ${SR_GPU_ROOT}/util/draw.cpp
        ${SR_GPU_ROOT}/packet_disassembler.cpp
        ${SR_GPU_ROOT}/primitive_processor.cpp
        ${SR_GPU_ROOT}/pipeline/render_target/cache.cpp
        ${SR_GPU_ROOT}/shared_memory.cpp
        ${SR_GPU_ROOT}/pipeline/texture/cache.cpp
        ${SR_GPU_ROOT}/pipeline/shader/interpreter.cpp
        ${SR_GPU_ROOT}/pipeline/shader/translator.cpp
        ${SR_GPU_ROOT}/pipeline/shader/translator_disasm.cpp
        ${SR_GPU_ROOT}/pipeline/shader/dxbc.cpp
        ${SR_GPU_ROOT}/pipeline/shader/dxbc_translator.cpp
        ${SR_GPU_ROOT}/pipeline/shader/dxbc_translator_alu.cpp
        ${SR_GPU_ROOT}/pipeline/shader/dxbc_translator_fetch.cpp
        ${SR_GPU_ROOT}/pipeline/shader/dxbc_translator_memexport.cpp
        ${SR_GPU_ROOT}/pipeline/shader/dxbc_translator_om.cpp
        ${SR_GPU_ROOT}/d3d12/graphics_system.cpp
        ${SR_GPU_ROOT}/d3d12/command_processor.cpp
        ${SR_GPU_ROOT}/d3d12/primitive_processor.cpp
        ${SR_GPU_ROOT}/d3d12/render_target_cache.cpp
        ${SR_GPU_ROOT}/d3d12/shader.cpp
        ${SR_GPU_ROOT}/d3d12/shared_memory.cpp
        ${SR_GPU_ROOT}/d3d12/texture_cache.cpp
        ${SR_GPU_ROOT}/d3d12/deferred_command_list.cpp
        ${SR_GPU_ROOT}/d3d12/pipeline_cache.cpp
        ${SR_REXGPU_SOURCE_DIR}/thirdparty/dxbc/DXBCChecksum.cpp
        src/native_renderer/sr_graphics_system.cpp
    )
    add_library(sr_gpu STATIC ${SR_GPU_SOURCES})
    target_include_directories(sr_gpu PRIVATE
        ${SR_REXGPU_SOURCE_DIR}
        ${SR_REXGPU_SOURCE_DIR}/thirdparty/renderdoc
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(sr_gpu PRIVATE -Wno-unused-variable -mssse3 -msse4.1)
    endif()
    target_link_libraries(sr_gpu PRIVATE rex::runtime rex::xxhash d3d12 dxgi dxguid)
    target_link_libraries(superman_returns PRIVATE sr_gpu)
    target_compile_definitions(superman_returns PRIVATE SR_HAS_TRACE_GPU=1)
else()
    target_compile_definitions(superman_returns PRIVATE SR_HAS_TRACE_GPU=0)
endif()
