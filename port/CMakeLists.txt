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
)

if(WIN32)
    add_executable(superman_returns WIN32 ${SUPERMANRETURNS_SOURCES})
else()
    add_executable(superman_returns ${SUPERMANRETURNS_SOURCES})
endif()

rexglue_setup_target(superman_returns GPU_PLUGINS xenos)
