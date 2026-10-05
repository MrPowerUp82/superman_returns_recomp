# Unit tests of the native renderer's pure parts: they need neither the game
# nor the ReXGlue SDK, so they build on Windows, Linux or macOS.
#   cmake -S tests/native -B build/tests-native && cmake --build build/tests-native
#   ctest --test-dir build/tests-native --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(sr_native_tests CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SR_NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../port/src/native_renderer")
add_executable(sr_native_tests
    main.cpp
    test_backend_selection.cpp
    test_checked_guest_memory.cpp
    test_guest_hash.cpp
    "${SR_NATIVE_DIR}/../graphics/backend_selection.cpp"
    test_shader_requirements.cpp
    test_guest_state.cpp
    test_render_packets.cpp
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
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(sr_native_tests PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME sr_native_tests COMMAND sr_native_tests)
