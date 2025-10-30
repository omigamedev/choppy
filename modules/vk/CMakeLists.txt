include(FetchContent)

if(WIN32)
else()
FetchContent_Declare(
    VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG 2cd90f9d20df57eac214c148f3aed885372ddcfe) # v1.4.321
FetchContent_MakeAvailable(VulkanHeaders)
find_package(Vulkan REQUIRED)
endif()

FetchContent_Declare(
    Volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG a8da8ef3368482b0ee9b0ec0c6079a16a89c6924) # vulkan-sdk-1.4.321
if (WIN32)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR)
elseif(ANDROID)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_ANDROID_KHR)
endif()
FetchContent_MakeAvailable(Volk)

FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG 1d8f600fd424278486eade7ed3e877c99f0846b1) # v3.3.0
FetchContent_MakeAvailable(VulkanMemoryAllocator)

add_library(vk OBJECT vma.cpp)
target_sources(vk PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES vk.cppm utils.cppm buffer.cppm shader.cppm texture.cppm)
target_link_libraries(vk PUBLIC volk VulkanMemoryAllocator platform glmcppm Tracy::TracyClient stb)
if(WIN32)
    target_compile_definitions(vk PUBLIC NOMINMAX)
else()
    target_link_libraries(vk PUBLIC Vulkan-Headers)
endif()