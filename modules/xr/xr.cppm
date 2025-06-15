module;

#ifdef __ANDROID__
#include <jni.h>
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#elifdef _WIN32
#define LOGE printf
#define LOGI printf
#endif

#include <cstdio>
#include <vector>
#include <algorithm>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

import std;

export module ce.xr;

export namespace ce::xr
{
class Instance
{
    XrInstance m_instance = XR_NULL_HANDLE;
    void print_info()
    {
        uint32_t count = 0;
        // Extensions
        xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
        std::vector ext_props(count, XrExtensionProperties{ .type = XR_TYPE_EXTENSION_PROPERTIES });
        xrEnumerateInstanceExtensionProperties(nullptr, count, &count, ext_props.data());
        std::sort(ext_props.begin(), ext_props.end(), [](auto& a, auto& b) { return std::string(a.extensionName) < std::string(b.extensionName); });
        for (const auto& e : ext_props)
            LOGI("XR Ext: %s\n", e.extensionName);
        // Layers
        xrEnumerateApiLayerProperties(0, &count, nullptr);
        std::vector layers_props(count, XrApiLayerProperties{ .type = XR_TYPE_API_LAYER_PROPERTIES });
        xrEnumerateApiLayerProperties(count, &count, layers_props.data());
        for (const auto& p : layers_props)
            LOGI("XR Api Layer: %s\n", p.layerName);
    }
public:
    bool create() noexcept
    {
        std::vector<const char*> extensions{
            XR_EXT_DEBUG_UTILS_EXTENSION_NAME,
            XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
            XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME,
            XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,
        };
        XrInstanceCreateInfo instance_info{
            .type = XR_TYPE_INSTANCE_CREATE_INFO,
            .next = nullptr,
            .createFlags = XrInstanceCreateFlags{0},
            .applicationInfo = XrApplicationInfo{
                .applicationName = "xrengine",
                .applicationVersion = 1,
                .engineName = "",
                .engineVersion = 0,
                .apiVersion = XR_CURRENT_API_VERSION,
            },
            .enabledApiLayerCount = 0,
            .enabledApiLayerNames = nullptr,
            .enabledExtensionCount = (uint32_t)(extensions.size()),
            .enabledExtensionNames = extensions.data(),
        };
        if (!XR_SUCCEEDED(xrCreateInstance(&instance_info, &m_instance)))
        {
            LOGE("xrCreateInstance failed");
            return false;
        }
        XrInstanceProperties instance_props{ .type = XR_TYPE_INSTANCE_PROPERTIES };
        xrGetInstanceProperties(m_instance, &instance_props);
        //LOGI(std::format("XR Instance {} - v{}.{}.{}",
        //    instance_props.runtimeName,
        //    XR_VERSION_MAJOR(instance_props.runtimeVersion),
        //    XR_VERSION_MINOR(instance_props.runtimeVersion),
        //    XR_VERSION_PATCH(instance_props.runtimeVersion));
        return true;
    }
};
struct Session
{

};

}
