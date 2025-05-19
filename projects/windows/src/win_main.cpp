#include <lib.h>

import std;
import vulkan_hpp;

#ifdef __INTELLISENSE__
#include <print>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>
#include <vulkan/vulkan_handles.hpp>
#endif

namespace vk::detail { DispatchLoaderDynamic defaultDispatchLoaderDynamic; }

int main()
{
    auto n = lib::foo(1, 2);
    vk::detail::defaultDispatchLoaderDynamic.init();
    std::uint32_t v = vk::enumerateInstanceVersion();
    std::println("Found Vulkan runtime {}.{}.{}", vk::versionMajor(v), vk::versionMinor(v), vk::versionPatch(v));
    const auto app_info = vk::ApplicationInfo{ "VulkanApp", 0, nullptr, 0, vk::ApiVersion14 };
    const auto instance_info = vk::InstanceCreateInfo{ vk::InstanceCreateFlags{}, &app_info, };
    auto instance = vk::createInstanceUnique(instance_info);
    vk::detail::defaultDispatchLoaderDynamic.init(*instance);
    return 0;
}
