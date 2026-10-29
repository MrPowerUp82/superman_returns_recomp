# Unit tests of the native renderer's pure parts: they need neither the game
# nor the ReXGlue SDK, so they build on Windows, Linux or macOS.
#   cmake -S tests/native -B build/tests-native && cmake --build build/tests-native
#   ctest --test-dir build/tests-native --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(sr_native_tests CXX)
find_package(Threads REQUIRED)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SR_NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../port/src/native_renderer")
add_executable(sr_native_tests
    main.cpp
    test_resource_write_tracking.cpp
    test_resource_tracking_api.cpp
    test_texture_write_proof.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/rexglue-write-tracking/src/write_tracking_state.cpp"
    test_benchmark_input.cpp
    test_descriptor_recycling.cpp
    test_buffer_srv_cache.cpp
    test_buffer_tracking_policy.cpp
    test_tracking_fast_view.cpp
    test_title_benchmark_policy.cpp
    test_mnk_bind_match.cpp
    test_costume_override_policy.cpp
    test_cpu_bundle.cpp
    test_xdk_draw_submission.cpp
    test_backend_selection.cpp
    test_checked_guest_memory.cpp
    test_idle_yield.cpp
    test_vblank_policy.cpp
    test_guest_render_scheduler.cpp
    test_guest_sleep_profile.cpp
    test_xma_flush_policy.cpp
    test_frame_generation_policy.cpp
    test_temporal_frame.cpp
    test_camera_candidate_probe.cpp
    test_surface_sequence.cpp
    test_geometry_submission_probe.cpp
    test_post_vertex_geometry.cpp
    test_graphics_options.cpp
    test_graphics_options_input.cpp
    test_graphics_options_guest_pause.cpp
    test_graphics_preferences.cpp
    test_shadow_projection.cpp
    test_readback_rows.cpp
    test_shadow_caster_geometry.cpp
    test_ray_shadow_execution.cpp
    test_temporal_geometry_history.cpp
    test_temporal_vertex_fetch.cpp
    test_static_world_shader_dependencies.cpp
    test_scene_color_boundary.cpp
    test_scene_color_geometry.cpp
    test_scene_color_vertex_input.cpp
    test_motion_depth_boundary.cpp
    test_scene_raster_jitter.cpp
    test_scene_resolution_shader.cpp
    test_scene_resolution_mapping.cpp
    test_observed_instance_lifetimes.cpp
    test_observed_skinned_motion.cpp
    test_observed_cloth_motion.cpp
    test_observed_skinned_part_key.cpp
    test_rt_shadow_shader.cpp
    test_pixel_coverage_shader.cpp
    test_motion_raster_coverage.cpp
    "${SR_NATIVE_DIR}/../graphics/pixel_coverage_shader.cpp"
    "${SR_NATIVE_DIR}/../graphics/scene_resolution_shader.cpp"
    "${SR_NATIVE_DIR}/../graphics/rt_shadow_shader.cpp"
    "${SR_NATIVE_DIR}/../graphics/graphics_preferences.cpp"
    "${SR_NATIVE_DIR}/../graphics/graphics_options.cpp"
    "${SR_NATIVE_DIR}/../graphics/graphics_options_runtime.cpp"
    test_guest_hash.cpp
    test_shared_constants_cache.cpp
    test_stacked_resolve.cpp
    test_texture_content.cpp
    test_texture_capture_cache.cpp
    test_texture_ranges.cpp
    test_texture_hash_replay.cpp
    test_texture_source_probe.cpp
    test_resource_unlock_audit.cpp
    test_frame_timeline.cpp
    "${SR_NATIVE_DIR}/../graphics/backend_selection.cpp"
    test_shader_requirements.cpp
    test_guest_state.cpp
    test_render_packets.cpp
    test_parallel_replay.cpp
    test_replay_options.cpp
    test_replay_trace.cpp
    "${SR_NATIVE_DIR}/../graphics/guest/replay_trace.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/parallel_replay.cpp"
    test_render_state.cpp
    test_vertex_layout.cpp
    test_primitive_expansion.cpp
    "${SR_NATIVE_DIR}/../graphics/guest/primitive_expansion.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/vertex_layout.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/render_state.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/edram_alias.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/render_packet.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/captured_batch.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/pm4_capture.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/draw_state.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/constant_snapshot.cpp"
    test_texture.cpp
    test_pm4_mirror.cpp
    test_shaders.cpp
    test_game_profile.cpp
    test_index_endian.cpp
    test_post_effects.cpp
    test_shader_library.cpp
    "${SR_NATIVE_DIR}/pm4_mirror.cpp"
)
target_include_directories(sr_native_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${SR_NATIVE_DIR}")
target_link_libraries(sr_native_tests PRIVATE Threads::Threads)
target_compile_definitions(sr_native_tests PRIVATE SR_REPLAY_TEST_HOOKS=1)
if(WIN32)
    target_sources(sr_native_tests PRIVATE test_replay_storage_writes.cpp)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(sr_native_tests PRIVATE -Wall -Wextra)
endif()

# XXH3 compiled with AVX2 (the only file built with -mavx2) against the baseline build. Needs the SDK's xxhash.h
# (.tools/rexglue-sdk); without it these tests are left out.
set(SR_XXHASH_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../.tools/rexglue-sdk/win-amd64/include")
if(WIN32 AND EXISTS "${SR_XXHASH_INCLUDE}/xxhash.h")
    target_sources(sr_native_tests PRIVATE test_xxh3_avx2.cpp xxh3_reference.cpp "${SR_NATIVE_DIR}/xxh3_avx2.cpp")
    # Texture mods hash with XXH3 (inlined) and read PNG with the vendored stb_image.
    target_sources(sr_native_tests PRIVATE test_texture_image.cpp test_texture_mods.cpp test_kingdom_come.cpp
        "${SR_NATIVE_DIR}/../graphics/texture_mods/texture_image.cpp"
        "${SR_NATIVE_DIR}/../graphics/texture_mods/texture_mods.cpp"
        "${SR_NATIVE_DIR}/../graphics/texture_mods/kingdom_come.cpp")
    set_source_files_properties("${SR_NATIVE_DIR}/../graphics/texture_mods/texture_image.cpp" PROPERTIES
        INCLUDE_DIRECTORIES "${SR_XXHASH_INCLUDE};${CMAKE_CURRENT_SOURCE_DIR}/../../port/third_party")
    # Paints costume skins over a texture dump, to look at the art without the game:
    # sr_skin_preview <dump dir> <out dir>.
    add_executable(sr_skin_preview skin_preview.cpp
        "${SR_NATIVE_DIR}/../graphics/texture_mods/texture_image.cpp"
        "${SR_NATIVE_DIR}/../graphics/texture_mods/texture_mods.cpp"
        "${SR_NATIVE_DIR}/../graphics/texture_mods/kingdom_come.cpp")
    target_include_directories(sr_skin_preview PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../port/src")
    set_source_files_properties("${SR_NATIVE_DIR}/xxh3_avx2.cpp" PROPERTIES
        COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/arch:AVX2,-mavx2>")
    set_source_files_properties(xxh3_reference.cpp "${SR_NATIVE_DIR}/xxh3_avx2.cpp" PROPERTIES
        INCLUDE_DIRECTORIES "${SR_XXHASH_INCLUDE}")
else()
    message(STATUS "xxhash.h not found: the AVX2 XXH3 tests are left out")
endif()

enable_testing()
add_executable(sr_dlss_contract_tests dlss_super_resolution_test.cpp
    "${SR_NATIVE_DIR}/../graphics/d3d12/dlss_super_resolution.cpp")
target_compile_definitions(sr_dlss_contract_tests PRIVATE SR_HAS_DLSS=0)
add_test(NAME sr_dlss_contract_tests COMMAND sr_dlss_contract_tests)
add_test(NAME sr_native_tests COMMAND sr_native_tests)
set_tests_properties(sr_native_tests PROPERTIES TIMEOUT 30)

add_executable(sr_capture_budget test_capture_budget.cpp
    "${SR_NATIVE_DIR}/../graphics/guest/pm4_capture.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/render_packet.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/captured_batch.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/render_state.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/constant_snapshot.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/draw_state.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/vertex_layout.cpp"
    "${SR_NATIVE_DIR}/pm4_mirror.cpp")
add_test(NAME sr_capture_budget COMMAND sr_capture_budget)

# Manual Release performance diagnostic; timing budgets do not belong in CTest.
add_executable(sr_replay_storage_bench replay_storage_benchmark.cpp
    "${SR_NATIVE_DIR}/../graphics/guest/parallel_replay.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/pm4_capture.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/render_packet.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/captured_batch.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/render_state.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/constant_snapshot.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/draw_state.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/vertex_layout.cpp"
    "${SR_NATIVE_DIR}/pm4_mirror.cpp")
target_link_libraries(sr_replay_storage_bench PRIVATE Threads::Threads)
add_executable(sr_pm4_budget test_pm4_budget.cpp
    "${SR_NATIVE_DIR}/pm4_mirror.cpp"
    "${SR_NATIVE_DIR}/../graphics/guest/constant_snapshot.cpp")
