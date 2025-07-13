include(FetchContent)
FetchContent_Declare(openxr
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG release-1.1.47)
FetchContent_MakeAvailable(openxr)
add_library(xr)
if (WIN32)
    target_compile_definitions(xr PUBLIC XR_USE_PLATFORM_WIN32 NOMINMAX)
else()
    target_compile_definitions(xr PUBLIC XR_USE_PLATFORM_ANDROID)
endif()
target_compile_definitions(xr PUBLIC
    XR_USE_GRAPHICS_API_VULKAN
    XR_EXTENSION_PROTOTYPES)
target_link_libraries(xr PUBLIC OpenXR::headers openxr_loader vk)
#target_include_directories(xr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/openxr-hpp)
target_sources(xr PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES xr.cppm)
