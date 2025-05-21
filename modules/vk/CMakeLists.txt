FetchContent_Declare(
    VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG v1.4.309)
set(VULKAN_HEADERS_ENABLE_MODULE ON)
FetchContent_MakeAvailable(VulkanHeaders)

find_package(Vulkan REQUIRED)
add_library(vk)
target_sources(vk PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES vk.cppm)
#target_sources(vk PUBLIC
#    FILE_SET CXX_MODULES
#    BASE_DIRS "$ENV{VULKAN_SDK}/Include/vulkan/"
#    FILES "$ENV{VULKAN_SDK}/Include/vulkan/vulkan.cppm")
if (WIN32)
    target_compile_definitions(vk PUBLIC
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_ENABLE_STD_MODULE=1
        VULKAN_HPP_STD_MODULE=std
        VK_USE_PLATFORM_WIN32_KHR=1)
elif(CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_compile_definitions(vk PUBLIC
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_ENABLE_STD_MODULE=1
        VULKAN_HPP_STD_MODULE=std
        VK_USE_PLATFORM_ANDROID_KHR=1)
endif()

target_link_libraries(vk PUBLIC std Vulkan::Vulkan Vulkan::VulkanHppModule)
