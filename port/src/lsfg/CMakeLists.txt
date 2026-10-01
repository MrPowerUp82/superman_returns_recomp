# Optional Windows LSFG backend and headless GPU validation tool.
if(NOT WIN32)
    message(FATAL_ERROR "SR_LSFG currently requires Windows")
endif()
set(SR_VULKAN_HEADERS "${PROJECT_SOURCE_DIR}/../.tools/vulkan-headers/include"
    CACHE PATH "Khronos Vulkan-Headers include directory")
if(NOT EXISTS "${SR_VULKAN_HEADERS}/vulkan/vulkan.h")
    message(FATAL_ERROR "SR_LSFG needs Vulkan-Headers: run tools/setup_lsfg.ps1")
endif()
set(LSFG_ROOT "${PROJECT_SOURCE_DIR}/third_party/lsfg-vk")
file(GLOB_RECURSE LSFG_SOURCES CONFIGURE_DEPENDS
    "${LSFG_ROOT}/lsfg-vk-backend/src/*.cpp"
    "${LSFG_ROOT}/lsfg-vk-common/src/vulkan/*.cpp")
add_library(sr_lsfg_backend STATIC ${LSFG_SOURCES}
    "${LSFG_ROOT}/lsfg-vk-common/src/helpers/errors.cpp")
target_include_directories(sr_lsfg_backend PUBLIC
    "${LSFG_ROOT}/lsfg-vk-backend/include"
    "${LSFG_ROOT}/lsfg-vk-common/include"
    "${SR_VULKAN_HEADERS}"
    PRIVATE "${LSFG_ROOT}/lsfg-vk-backend/src")
target_compile_definitions(sr_lsfg_backend PUBLIC VK_USE_PLATFORM_WIN32_KHR NOMINMAX)
target_compile_features(sr_lsfg_backend PUBLIC cxx_std_23)
add_library(sr_lsfg_interop STATIC win32_interop.cpp)
target_link_libraries(sr_lsfg_interop PUBLIC sr_lsfg_backend d3d12 dxgi)
target_include_directories(sr_lsfg_interop PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(sr_lsfg_smoke smoke.cpp)
target_link_libraries(sr_lsfg_smoke PRIVATE sr_lsfg_interop)
