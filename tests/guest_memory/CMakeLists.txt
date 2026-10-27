cmake_minimum_required(VERSION 3.25)
project(sr_guest_memory_tests LANGUAGES CXX)
find_package(rexglue 0.10.0 REQUIRED CONFIG)
add_executable(sr_guest_memory_tests ../native/main.cpp test_physical_allocation.cpp
  ../../port/src/guest_physical_allocation.cpp)
target_compile_features(sr_guest_memory_tests PRIVATE cxx_std_23)
target_link_libraries(sr_guest_memory_tests PRIVATE rex::runtime)
add_custom_command(TARGET sr_guest_memory_tests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:rex::runtime> $<TARGET_FILE_DIR:sr_guest_memory_tests>)
enable_testing()
add_test(NAME sr_guest_memory_tests COMMAND sr_guest_memory_tests)
set_tests_properties(sr_guest_memory_tests PROPERTIES TIMEOUT 45)
