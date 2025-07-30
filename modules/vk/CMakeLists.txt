include(FetchContent)

if(WIN32)
else()
FetchContent_Declare(
    VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG v1.4.321)
FetchContent_MakeAvailable(VulkanHeaders)
find_package(Vulkan REQUIRED)
endif()

FetchContent_Declare(
    Volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG vulkan-sdk-1.4.321)
if (WIN32)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR)
elseif()
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_ANDROID_KHR)
endif()
FetchContent_MakeAvailable(Volk)

FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.3.0)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

add_library(vk OBJECT)
target_sources(vk PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES vk.cppm utils.cppm)
target_link_libraries(vk PUBLIC volk VulkanMemoryAllocator platform)
target_compile_definitions(vk PRIVATE VMA_IMPLEMENTATION)
if(WIN32)
    target_compile_definitions(vk PUBLIC NOMINMAX)
else()
    target_link_libraries(vk PUBLIC Vulkan-Headers)
endif()