# Out-of-tree build of the XenosRecomp HLSL emitter for the game's shader corpus.
# Ported from crazyriddler/rexglue-native-kit @136bc6c4, tools/xenosrecomp/CMakeLists.txt.
# Changes: the patched source and its fmt/xxHash dependencies live under
# .tools/xenosrecomp (fetched by tools/shaders/fetch_xenosrecomp.ps1) instead
# of the kit's vendored src/ and SDK thirdparty.
cmake_minimum_required(VERSION 3.20)
project(XenosRecompCorpus CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(XR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.tools/xenosrecomp" CACHE PATH
    "Fetched XenosRecomp tree (tools/shaders/fetch_xenosrecomp.ps1)")
set(XR_SRC "${XR_ROOT}/src/XenosRecomp")
set(XR_DEPS "${XR_ROOT}/deps")
if (NOT EXISTS "${XR_SRC}/shader_recompiler.cpp")
    message(FATAL_ERROR "${XR_SRC} is missing: run tools/shaders/fetch_xenosrecomp.ps1")
endif()

# Game-specific semantics switch (see patches/). OFF = upstream generic behavior.
# CONAN_RECOMP = the native-renderer feature set developed on Conan (shared constants,
# TEX_SCALE, PWL gamma, spec constants, loops/bools fixes...). Mostly generic;
# see docs/native-port-plan.md section 6 for the few Conan-specific heuristics.
option(CONAN_RECOMP "Enable the native-port XenosRecomp feature set" ON)

add_executable(XenosRecompCorpus
    corpus_main.cpp
    "${XR_SRC}/shader_recompiler.cpp")
target_include_directories(XenosRecompCorpus PRIVATE
    "${XR_SRC}"
    "${XR_DEPS}/fmt/include"
    "${XR_DEPS}/xxHash")
target_compile_definitions(XenosRecompCorpus PRIVATE FMT_HEADER_ONLY _CRT_SECURE_NO_WARNINGS)
if (CONAN_RECOMP)
    target_compile_definitions(XenosRecompCorpus PRIVATE CONAN_RECOMP)
endif()
# Keep asserts enabled even in Release: assertion text is the failure reason
# recorded in the catalog.
target_compile_options(XenosRecompCorpus PRIVATE -UNDEBUG -Wno-switch -Wno-unused-variable
    -Wno-null-arithmetic -fms-extensions -Wno-deprecated-declarations)
target_precompile_headers(XenosRecompCorpus PRIVATE pch_corpus.h)

include(CTest)
if(BUILD_TESTING AND CONAN_RECOMP)
    add_executable(XenosRecompTests
        test_recompiler.cpp
        "${XR_SRC}/shader_recompiler.cpp")
    target_include_directories(XenosRecompTests PRIVATE
        "${XR_SRC}" "${XR_DEPS}/fmt/include" "${XR_DEPS}/xxHash")
    target_compile_definitions(XenosRecompTests PRIVATE
        CONAN_RECOMP FMT_HEADER_ONLY _CRT_SECURE_NO_WARNINGS)
    target_compile_options(XenosRecompTests PRIVATE -UNDEBUG -Wno-switch
        -Wno-unused-variable -Wno-null-arithmetic -fms-extensions
        -Wno-deprecated-declarations)
    target_precompile_headers(XenosRecompTests REUSE_FROM XenosRecompCorpus)
    add_test(NAME xenos_recompiler_alu COMMAND XenosRecompTests)
endif()
