module;

#ifdef __ANDROID__
#include <jni.h>
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#elifdef _WIN32
#include <windows.h>
#define LOGE(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

#include <cstdio>
#include <vector>
#include <algorithm>
#include <memory>
#include <ranges>
#include <thread>
#include <span>
#include <volk.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_to_string.hpp>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

export module ce.xr;

export namespace ce::xr
{
class Instance
{
    XrInstance m_instance = XR_NULL_HANDLE;
    XrSystemId m_system;
    XrSession m_session = XR_NULL_HANDLE;
    XrSpace m_space = XR_NULL_HANDLE;
    VkInstance m_vk_instance = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
    const uint32_t m_queue_index = 0;
    uint32_t m_queue_family_index = std::numeric_limits<uint32_t>::max();
    XrSwapchain color_swapchain = XR_NULL_HANDLE;
    XrSwapchain depth_swapchain = XR_NULL_HANDLE;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    std::vector<XrSwapchainImageVulkanKHR> color_swapchain_images;
    std::vector<XrSwapchainImageVulkanKHR> depth_swapchain_images;
    std::vector<XrViewConfigurationView> views;
#ifdef __ANDROID__
    jobject m_android_context = nullptr;
    JavaVM* m_android_vm = nullptr;
#endif
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
    void wait_state(XrSessionState wait_state) const noexcept
    {
        XrEventDataBuffer event_data{ .type = XR_TYPE_EVENT_DATA_BUFFER };
        XrResult r{};
        while (true)
        {
            r = xrPollEvent(m_instance, &event_data);
            if (r == XR_SUCCESS)
            {
                if (event_data.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
                {
                    const auto* e = reinterpret_cast<XrEventDataSessionStateChanged*>(&event_data);
                    if (e->state == wait_state)
                        break;
                }
            }
            else if (r == XR_EVENT_UNAVAILABLE)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            else
            {
                break;
            }
            event_data.type = XR_TYPE_EVENT_DATA_BUFFER;
        }
    }
    [[nodiscard]] const char* to_string(XrResult r) const noexcept
    {
        static char sr[XR_MAX_RESULT_STRING_SIZE]{0};
        xrResultToString(m_instance, r, sr);
        return sr;
    }
    [[nodiscard]] const char* to_string(VkResult r) const noexcept
    {
        static char sr[64]{0};
        const std::string ss = vk::to_string(static_cast<vk::Result>(r));
        std::copy_n(ss.begin(), std::min(ss.size(), sizeof(sr) - 1), sr);
        return sr;
    }
    [[nodiscard]] std::vector<std::string> split_string(std::string_view str, char delimiter = ' ') const noexcept
    {
        auto tokens = str
            | std::views::split(delimiter)
            | std::views::transform([](auto&& subrange) {
                return std::string(subrange.begin(), subrange.end());
            });

        return { tokens.begin(), tokens.end() };
    }
    template<typename T>
    struct XrFunction
    {
        T ptr;
        XrFunction(XrInstance i, const char* name)
        {
            xrGetInstanceProcAddr(i, name, reinterpret_cast<PFN_xrVoidFunction*>(&ptr));
        }
    };
    XRAPI_ATTR XrResult XRAPI_CALL xrCreateVulkanInstanceKHR(
        const XrVulkanInstanceCreateInfoKHR*        createInfo,
        VkInstance*                                 vulkanInstance,
        VkResult*                                   vulkanResult)
    {
        static XrFunction<PFN_xrCreateVulkanInstanceKHR> fn{m_instance, "xrCreateVulkanInstanceKHR"};
        return fn.ptr(m_instance, createInfo, vulkanInstance, vulkanResult);
    }
    XRAPI_ATTR XrResult XRAPI_CALL xrCreateVulkanDeviceKHR(
        const XrVulkanDeviceCreateInfoKHR*          createInfo,
        VkDevice*                                   vulkanDevice,
        VkResult*                                   vulkanResult)
    {
        static XrFunction<PFN_xrCreateVulkanDeviceKHR> fn{m_instance, "xrCreateVulkanDeviceKHR"};
        return fn.ptr(m_instance, createInfo, vulkanDevice, vulkanResult);
    }
    XRAPI_ATTR XrResult XRAPI_CALL xrGetVulkanGraphicsDevice2KHR(
        const XrVulkanGraphicsDeviceGetInfoKHR*     getInfo,
        VkPhysicalDevice*                           vulkanPhysicalDevice)
    {
        static XrFunction<PFN_xrGetVulkanGraphicsDevice2KHR> fn{m_instance, "xrGetVulkanGraphicsDevice2KHR"};
        return fn.ptr(m_instance, getInfo, vulkanPhysicalDevice);
    }
    XRAPI_ATTR XrResult XRAPI_CALL xrGetVulkanGraphicsRequirements2KHR(
        XrGraphicsRequirementsVulkanKHR*            graphicsRequirements)
    {
        static XrFunction<PFN_xrGetVulkanGraphicsRequirements2KHR> fn{m_instance, "xrGetVulkanGraphicsRequirements2KHR"};
        return fn.ptr(m_instance, m_system, graphicsRequirements);
    }
    XRAPI_ATTR XrResult XRAPI_CALL xrInitializeLoaderKHR(
            const XrLoaderInitInfoBaseHeaderKHR*        loaderInitInfo)
    {
        static XrFunction<PFN_xrInitializeLoaderKHR> fn{XR_NULL_HANDLE, "xrInitializeLoaderKHR"};
        return fn.ptr(loaderInitInfo);
    }

public:
    [[nodiscard]] VkInstance vk_instance() const noexcept { return m_vk_instance; }
    [[nodiscard]] VkDevice device() const noexcept { return m_device; }
    [[nodiscard]] VkPhysicalDevice physical_device() const noexcept { return m_physical_device; }
    [[nodiscard]] XrSession session() const noexcept { return m_session; }
    [[nodiscard]] XrSpace space() const noexcept { return m_space; }
    [[nodiscard]] uint32_t queue_family_index() const noexcept { return m_queue_family_index; }

#ifdef __ANDROID__
    void setup_android(JavaVM* vm, jobject context)
    {
        m_android_context = context;
        m_android_vm = vm;
    }
#endif
    bool create() noexcept
    {
        //print_info();

#ifdef __ANDROID__
        const XrLoaderInitInfoAndroidKHR loader_init {
            .type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR,
            .applicationVM = m_android_vm,
            .applicationContext = m_android_context,
        };
        if (XrResult result = xrInitializeLoaderKHR(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loader_init)); !XR_SUCCEEDED(result))
        {
            LOGE("xrInitializeLoaderKHR failed: %s", to_string(result));
            return false;
        }
#endif

        // Create Instance

        const std::vector<const char*> extensions{
            //XR_EXT_DEBUG_UTILS_EXTENSION_NAME,
            //XR_KHR_VULKAN_ENABLE_EXTENSION_NAME,
            XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
//#ifdef __ANDROID__
//            XR_KHR_LOADER_INIT_EXTENSION_NAME,
//#endif
            //XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME,
            //XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,
        };
        const XrInstanceCreateInfo instance_info{
            .type = XR_TYPE_INSTANCE_CREATE_INFO,
            .next = nullptr,
            .createFlags = XrInstanceCreateFlags{0},
            .applicationInfo = XrApplicationInfo{
                .applicationName = "choppy_engine",
                .applicationVersion = 1,
                .apiVersion = XR_CURRENT_API_VERSION,
            },
            .enabledApiLayerCount = 0,
            .enabledApiLayerNames = nullptr,
            .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
            .enabledExtensionNames = extensions.data(),
        };
        if (XrResult result = xrCreateInstance(&instance_info, &m_instance); !XR_SUCCEEDED(result))
        {
            LOGE("xrCreateInstance failed: %s", to_string(result));
            return false;
        }
        XrInstanceProperties instance_props{.type = XR_TYPE_INSTANCE_PROPERTIES};
        if (XrResult result = xrGetInstanceProperties(m_instance, &instance_props); !XR_SUCCEEDED(result))
        {
            LOGE("xrGetInstanceProperties failed: %s", to_string(result));
            return false;
        }

        // Get System

        constexpr XrSystemGetInfo system_info{
            .type = XR_TYPE_SYSTEM_GET_INFO,
            .formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY,
        };
        if (!XR_SUCCEEDED(xrGetSystem(m_instance, &system_info, &m_system)))
        {
            LOGE("xrGetSystem failed");
            return false;
        }
        XrSystemProperties system_props{.type = XR_TYPE_SYSTEM_PROPERTIES};
        if (XrResult result = xrGetSystemProperties(m_instance, m_system, &system_props); !XR_SUCCEEDED(result))
        {
            LOGE("xrGetSystemProperties failed: %s", to_string(result));
            return false;
        }
        LOGI("System: %s", system_props.systemName);

        // Create Vulkan Instance

        XrGraphicsRequirementsVulkan2KHR vulkan_req{.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
        if (XrResult result = xrGetVulkanGraphicsRequirements2KHR(&vulkan_req); !XR_SUCCEEDED(result))
        {
            LOGE("xrGetVulkanGraphicsRequirements2KHR failed: %s", to_string(result));
            return false;
        }
        uint32_t vk_req_version = VK_MAKE_VERSION(
            XR_VERSION_MAJOR(vulkan_req.maxApiVersionSupported),
            XR_VERSION_MINOR(vulkan_req.maxApiVersionSupported),
            XR_VERSION_PATCH(vulkan_req.maxApiVersionSupported));
        volkInitialize();
        std::uint32_t vk_runtime_version = 0;
        if (VkResult result = vkEnumerateInstanceVersion(&vk_runtime_version); result != VK_SUCCESS)
        {
            LOGE("vkEnumerateInstanceVersion failed: %s", to_string(result));
            return false;
        }
        uint32_t vk_version = std::min(vk_req_version, vk_runtime_version);
        const VkApplicationInfo app_info{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "choppy_engine",
            .applicationVersion = 0,
            .apiVersion = vk_version
        };
        const VkInstanceCreateInfo vk_instance_info{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &app_info,
        };
        XrVulkanInstanceCreateInfoKHR vk_create_info{
            .type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR,
            .systemId = m_system,
            .pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(vkGetInstanceProcAddr),
            .vulkanCreateInfo = &vk_instance_info
        };
        VkResult vk_create_result;
        if (XrResult result = xrCreateVulkanInstanceKHR(&vk_create_info, &m_vk_instance, &vk_create_result); !XR_SUCCEEDED(result))
        {
            LOGE("xrCreateVulkanInstanceKHR failed: %s", to_string(result));
            return false;
        }
        if (vk_create_result != VK_SUCCESS)
        {
            LOGE("vkCreateInstance failed: %s", to_string(vk_create_result));
            return false;
        }
        volkLoadInstance(m_vk_instance);

        // Retrieve Physical Device

        const XrVulkanGraphicsDeviceGetInfoKHR info{
            .type = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR,
            .systemId = m_system,
            .vulkanInstance = m_vk_instance
        };
        if (XrResult result = xrGetVulkanGraphicsDevice2KHR(&info, &m_physical_device); !XR_SUCCEEDED(result))
        {
            LOGE("xrGetVulkanGraphicsDevice2KHR failed: %s", to_string(result));
            return false;
        }
        VkPhysicalDeviceProperties2 physical_device_properties{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        vkGetPhysicalDeviceProperties2(m_physical_device, &physical_device_properties);
        LOGI("Vulkan device: %s", physical_device_properties.properties.deviceName);

        // Create Device

        const std::vector<VkQueueFamilyProperties2> queue_props = [this]{
            uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties2(m_physical_device, &count, nullptr);
            std::vector<VkQueueFamilyProperties2> props(count, {.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2});
            vkGetPhysicalDeviceQueueFamilyProperties2(m_physical_device, &count, props.data());
            return props;
        }();

        for (int i = 0; i < queue_props.size(); i++)
        {
            const VkQueueFlags queue_flags = queue_props[i].queueFamilyProperties.queueFlags;
            const bool has_graphics = queue_flags & VK_QUEUE_GRAPHICS_BIT;
            const bool has_compute = queue_flags & VK_QUEUE_COMPUTE_BIT;
            if (has_graphics && has_compute)
            {
                m_queue_family_index = i;
                break;
            }
        }
        if (m_queue_family_index == std::numeric_limits<uint32_t>::max())
        {
            LOGE("Failed to find a suitable queue family");
            return false;
        }

        constexpr std::array queue_priority{1.f};
        const VkDeviceQueueCreateInfo queue_info{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = m_queue_family_index,
            .queueCount = static_cast<uint32_t>(queue_priority.size()),
            .pQueuePriorities = queue_priority.data()
        };
        const VkDeviceCreateInfo device_info{
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queue_info,
        };
        XrVulkanDeviceCreateInfoKHR device_create_info{
            .type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR,
            .systemId = m_system,
            .pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(vkGetInstanceProcAddr),
            .vulkanPhysicalDevice = m_physical_device,
            .vulkanCreateInfo = &device_info
        };
        if (XrResult result = xrCreateVulkanDeviceKHR(&device_create_info, &m_device, &vk_create_result); !XR_SUCCEEDED(result))
        {
            LOGE("xrCreateVulkanDeviceKHR failed: %s", to_string(result));
            return false;
        }
        if (vk_create_result != VK_SUCCESS)
        {
            LOGE("vkCreateDevice failed: %s", to_string(vk_create_result));
            return false;
        }
        volkLoadDevice(m_device);

        return true;
    }
    bool start_session()
    {
        const XrGraphicsBindingVulkanKHR binding{
            .type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR,
            .instance = m_vk_instance,
            .physicalDevice = m_physical_device,
            .device = m_device,
            .queueFamilyIndex = m_queue_family_index,
            .queueIndex = m_queue_index
        };
        const XrSessionCreateInfo info{
            .type = XR_TYPE_SESSION_CREATE_INFO,
            .next = &binding,
            .systemId = m_system
        };
        if (XrResult result = xrCreateSession(m_instance, &info, &m_session); !XR_SUCCEEDED(result))
        {
            LOGE("xrCreateSession failed: %s", to_string(result));
            return false;
        }
        constexpr XrReferenceSpaceCreateInfo space_info{
            .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
            .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL,
            .poseInReferenceSpace = XrPosef{
                .orientation = { .x = 0, .y = 0, .z = 0, .w = 1.0 },
                .position = { .x = 0, .y = 0, .z = 0 }
            }
        };
        if (XrResult result = xrCreateReferenceSpace(m_session, &space_info, &m_space); !XR_SUCCEEDED(result))
        {
            LOGE("xrCreateReferenceSpace failed: %s", to_string(result));
            return false;
        }
        wait_state(XR_SESSION_STATE_READY);
        constexpr XrSessionBeginInfo begin_info{
            .type = XR_TYPE_SESSION_BEGIN_INFO,
            .primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO
        };
        if (XrResult result = xrBeginSession(m_session, &begin_info); !XR_SUCCEEDED(result))
        {
            LOGE("xrBeginSession failed: %s", to_string(result));
            return false;
        }
        return true;
    }
    [[nodiscard]] VkFormat find_format(std::span<const VkFormat> formats, VkFormatFeatureFlags features) const noexcept
    {
        for (const auto format : formats)
        {
            VkFormatProperties2 props{.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};
            vkGetPhysicalDeviceFormatProperties2(m_physical_device, format, &props);
            if (props.formatProperties.optimalTilingFeatures & features)
                return format;
        }
        return VK_FORMAT_UNDEFINED;
    }
    bool create_swapchain() noexcept
    {
        const std::vector<VkFormat> supported_color_formats = [this]{
            uint32_t count = 0;
            xrEnumerateSwapchainFormats(m_session, 0, &count, nullptr);
            std::vector<int64_t> raw_values(count, VK_FORMAT_UNDEFINED);
            xrEnumerateSwapchainFormats(m_session, count, &count, raw_values.data());
            return raw_values
                | std::views::transform([](int64_t v) { return static_cast<VkFormat>(v); })
                | std::ranges::to<std::vector<VkFormat>>();
        }();
        const std::array desired_depth_formats{
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D16_UNORM,
        };
        depth_format = find_format(desired_depth_formats,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        color_format = find_format(supported_color_formats,
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
        views = [this]{
            uint32_t count = 0;
            xrEnumerateViewConfigurationViews(m_instance, m_system,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr);
            std::vector<XrViewConfigurationView> views(count,
                {.type = XR_TYPE_VIEW_CONFIGURATION_VIEW});
            xrEnumerateViewConfigurationViews(m_instance, m_system,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, static_cast<uint32_t>(views.size()),
                &count, views.data());
            return views;
        }();

        // Color swapchain

        const XrSwapchainCreateInfo color_swapchain_info{
            .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
            .usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
            .format = color_format,
            .sampleCount = views[0].recommendedSwapchainSampleCount,
            .width = views[0].recommendedImageRectWidth,
            .height = views[0].recommendedImageRectHeight,
            .faceCount = 1,
            .arraySize = static_cast<uint32_t>(views.size()),
            .mipCount = 1
        };
        if (XrResult result = xrCreateSwapchain(m_session, &color_swapchain_info, &color_swapchain); !XR_SUCCEEDED(result))
        {
            LOGE("xrCreateSwapchain failed: %s", to_string(result));
            return false;
        }
        color_swapchain_images = [this] {
            uint32_t count = 0;
            xrEnumerateSwapchainImages(color_swapchain, 0, &count, nullptr);
            std::vector<XrSwapchainImageVulkanKHR> images(count,
                XrSwapchainImageVulkanKHR{.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
            xrEnumerateSwapchainImages(color_swapchain, count, &count,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
            return images;
        }();

        // Depth swapchain

        const XrSwapchainCreateInfo depth_swapchain_info{
            .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
            .usageFlags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .format = depth_format,
            .sampleCount = views[0].recommendedSwapchainSampleCount,
            .width = views[0].recommendedImageRectWidth,
            .height = views[0].recommendedImageRectHeight,
            .faceCount = 1,
            .arraySize = static_cast<uint32_t>(views.size()),
            .mipCount = 1
        };
        if (XrResult result = xrCreateSwapchain(m_session, &depth_swapchain_info, &depth_swapchain); !XR_SUCCEEDED(result))
        {
            LOGE("xrCreateSwapchain failed: %s", to_string(result));
            return false;
        }
        depth_swapchain_images = [this] {
            uint32_t count = 0;
            xrEnumerateSwapchainImages(color_swapchain, 0, &count, nullptr);
            std::vector<XrSwapchainImageVulkanKHR> images(count,
                XrSwapchainImageVulkanKHR{.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
            xrEnumerateSwapchainImages(color_swapchain, count, &count,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
            return images;
        }();

        return true;
    }
};
struct Session
{

};

}
