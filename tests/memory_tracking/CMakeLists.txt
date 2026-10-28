cmake_minimum_required(VERSION 3.25)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(sr_memory_tracking_tests LANGUAGES CXX)
  set(REXSDK_DIR "" CACHE PATH "Optional patched SDK sources")
  if(REXSDK_DIR)
    add_compile_options(-march=x86-64-v2)
    add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
  else()
    find_package(rexglue 0.10.0 REQUIRED CONFIG)
  endif()
endif()
option(SR_MEMORY_TRACKING_STOCK_CLIENT "Test only client fallback against the stock SDK" OFF)
add_executable(sr_memory_tracking_tests main.cpp ../../port/src/native_renderer/resource_write_tracking.cpp)
if(SR_MEMORY_TRACKING_STOCK_CLIENT)
  target_sources(sr_memory_tracking_tests PRIVATE test_client_stock.cpp)
  target_compile_definitions(sr_memory_tracking_tests PRIVATE SR_TRACKING_STOCK_CLIENT=1)
else()
  target_sources(sr_memory_tracking_tests PRIVATE test_runtime_tracking.cpp test_runtime_io.cpp test_texture_upload.cpp test_texture_gpu.cpp ../native/test_texture_layout.cpp
    ../../port/src/native_renderer/texture_decode.cpp
    ../../port/src/graphics/texture_mods/texture_image.cpp
    ../../port/src/graphics/texture_mods/texture_mods.cpp
    ../../port/src/graphics/texture_mods/texture_mods_runtime.cpp
    ../../port/src/graphics/texture_mods/costume_skin.cpp
    ../../port/src/graphics/guest/texture_capture.cpp
    ../../port/src/graphics/guest/texture_layout.cpp
    ../../port/src/graphics/guest/captured_batch.cpp)
endif()
target_compile_features(sr_memory_tracking_tests PRIVATE cxx_std_23)
# texture_decode.cpp applies texture mods (stb_image is vendored under port/third_party).
target_include_directories(sr_memory_tracking_tests PRIVATE ../../port/third_party)
target_link_libraries(sr_memory_tracking_tests PRIVATE rex::runtime)
if(TARGET rex::xxhash)
  target_link_libraries(sr_memory_tracking_tests PRIVATE rex::xxhash)
elseif(TARGET xxhash)
  target_link_libraries(sr_memory_tracking_tests PRIVATE xxhash)
endif()
if(WIN32)
  target_link_libraries(sr_memory_tracking_tests PRIVATE ws2_32 d3d12 dxgi)
endif()
add_custom_command(TARGET sr_memory_tracking_tests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:rex::runtime> $<TARGET_FILE_DIR:sr_memory_tracking_tests>)
enable_testing()
add_test(NAME sr_memory_tracking_tests COMMAND sr_memory_tracking_tests)
set_tests_properties(sr_memory_tracking_tests PROPERTIES TIMEOUT 45)
if(NOT SR_MEMORY_TRACKING_STOCK_CLIENT)
  add_test(NAME sr_memory_tracking_lost_contract COMMAND sr_memory_tracking_tests --lost-contract)
  add_test(NAME sr_memory_tracking_arm_failure COMMAND sr_memory_tracking_tests --arm-failure)
  add_test(NAME sr_memory_tracking_uninitialized COMMAND sr_memory_tracking_tests --uninitialized)
  set_tests_properties(sr_memory_tracking_lost_contract sr_memory_tracking_arm_failure sr_memory_tracking_uninitialized PROPERTIES TIMEOUT 15)
endif()
