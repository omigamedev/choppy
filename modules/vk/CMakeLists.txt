
#find_package(Vulkan)
add_library(vk)
#target_sources(vk PUBLIC
#    FILE_SET CXX_MODULES
#    BASE_DIRS "$ENV{VULKAN_SDK}/Include/vulkan/"
#    FILES "$ENV{VULKAN_SDK}/Include/vulkan/vulkan.cppm")
target_compile_definitions(vk PUBLIC
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
    VULKAN_HPP_ENABLE_STD_MODULE=1
    VULKAN_HPP_STD_MODULE=std
    VK_USE_PLATFORM_WIN32_KHR=1)
target_link_libraries(vk PUBLIC std Vulkan::Vulkan)
