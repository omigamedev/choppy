module;

#ifdef __ANDROID__
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#elifdef _WIN32
#define LOGE std::print
#define LOGI std::print
#endif

#include <cstdint>

export module vk;
export import vulkan_hpp;
export import std;
namespace vk::detail { DispatchLoaderDynamic defaultDispatchLoaderDynamic; }
namespace choppy
{
export class VulkanContext
{
public:
    bool create()
    {
        vk::detail::defaultDispatchLoaderDynamic.init();
        std::uint32_t v = vk::enumerateInstanceVersion();
        LOGI("Found Vulkan runtime %d.%d.%d", vk::versionMajor(v), vk::versionMinor(v), vk::versionPatch(v));
        const auto app_info = vk::ApplicationInfo{ "VulkanApp", 0, nullptr, 0, vk::ApiVersion14 };
        const auto instance_info = vk::InstanceCreateInfo{ vk::InstanceCreateFlags{}, &app_info, };
        auto instance = vk::createInstanceUnique(instance_info);
        vk::detail::defaultDispatchLoaderDynamic.init(*instance);
        return true;
    }
};
}