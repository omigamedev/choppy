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
#include <print>
#include <ranges>
#include <thread>
#include <vulkan/vulkan.h>
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
    VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
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
    void wait_state(XrSessionState wait_state)
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
                    auto* e = reinterpret_cast<XrEventDataSessionStateChanged*>(&event_data);
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
    const char* to_string(XrResult r)
    {
        static char sr[XR_MAX_RESULT_STRING_SIZE];
        xrResultToString(m_instance, r, sr);
        return sr;
    }
    std::vector<std::string> split_string(std::string_view str, char delimiter = ' ')
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
    XRAPI_ATTR XrResult XRAPI_CALL xrGetVulkanInstanceExtensionsKHR(
        uint32_t                                    bufferCapacityInput,
        uint32_t*                                   bufferCountOutput,
        char*                                       buffer)
    {
        static XrFunction<PFN_xrGetVulkanInstanceExtensionsKHR> fn{m_instance, "xrGetVulkanInstanceExtensionsKHR"};
        return fn.ptr(m_instance, m_system, bufferCapacityInput, bufferCountOutput, buffer);
    }
    XRAPI_ATTR XrResult XRAPI_CALL xrGetVulkanDeviceExtensionsKHR(
        uint32_t                                    bufferCapacityInput,
        uint32_t*                                   bufferCountOutput,
        char*                                       buffer)
    {
        static XrFunction<PFN_xrGetVulkanDeviceExtensionsKHR> fn{m_instance, "xrGetVulkanDeviceExtensionsKHR"};
        return fn.ptr(m_instance, m_system, bufferCapacityInput, bufferCountOutput, buffer);
    }
    XRAPI_ATTR XrResult XRAPI_CALL xrInitializeLoaderKHR(
            const XrLoaderInitInfoBaseHeaderKHR*        loaderInitInfo)
    {
        static XrFunction<PFN_xrInitializeLoaderKHR> fn{XR_NULL_HANDLE, "xrInitializeLoaderKHR"};
        return fn.ptr(loaderInitInfo);
    }

    public:
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
        xrInitializeLoaderKHR(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loader_init));
#endif

        // Create Instance

        std::vector<const char*> extensions{
            //XR_EXT_DEBUG_UTILS_EXTENSION_NAME,
            XR_KHR_VULKAN_ENABLE_EXTENSION_NAME,
            XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
//#ifdef __ANDROID__
//            XR_KHR_LOADER_INIT_EXTENSION_NAME,
//#endif
            //XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME,
            //XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,
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
            .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
            .enabledExtensionNames = extensions.data(),
        };
        if (!XR_SUCCEEDED(xrCreateInstance(&instance_info, &m_instance)))
        {
            LOGE("xrCreateInstance failed");
            return false;
        }
        XrInstanceProperties instance_props{ .type = XR_TYPE_INSTANCE_PROPERTIES };
        xrGetInstanceProperties(m_instance, &instance_props);

        // Get System

        XrSystemGetInfo system_info{
            .type = XR_TYPE_SYSTEM_GET_INFO,
            .formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY,
        };
        if (!XR_SUCCEEDED(xrGetSystem(m_instance, &system_info, &m_system)))
        {
            LOGE("xrGetSystem failed");
            return false;
        }
        XrSystemProperties system_props{ .type = XR_TYPE_SYSTEM_PROPERTIES };
        xrGetSystemProperties(m_instance, m_system, &system_props);
        LOGI("System: %s", system_props.systemName);

        return true;
    }
    [[nodiscard]] XrVersion vulkan_version()
    {
        XrGraphicsRequirementsVulkan2KHR vulkan_req{ .type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR };
        auto result = xrGetVulkanGraphicsRequirements2KHR(&vulkan_req);
        LOGI("XR Vulkan Min supported v%d.%d.%d",
            XR_VERSION_MAJOR(vulkan_req.minApiVersionSupported),
            XR_VERSION_MINOR(vulkan_req.minApiVersionSupported),
            XR_VERSION_PATCH(vulkan_req.minApiVersionSupported));
        LOGI("XR Vulkan Max supported v%d.%d.%d",
            XR_VERSION_MAJOR(vulkan_req.maxApiVersionSupported),
            XR_VERSION_MINOR(vulkan_req.maxApiVersionSupported),
            XR_VERSION_PATCH(vulkan_req.maxApiVersionSupported));
        return vulkan_req.maxApiVersionSupported;
    }
    [[nodiscard]] std::vector<std::string> instance_extensions() noexcept
    {
        uint32_t num_extensions = 0;
        xrGetVulkanInstanceExtensionsKHR(0, &num_extensions, nullptr);
        std::string extensions(static_cast<size_t>(num_extensions), '\0');
        xrGetVulkanInstanceExtensionsKHR(extensions.size(), &num_extensions, extensions.data());
        return split_string(extensions);
    }
    [[nodiscard]] std::vector<std::string> device_extensions() noexcept
    {
        uint32_t num_extensions = 0;
        xrGetVulkanDeviceExtensionsKHR(0, &num_extensions, nullptr);
        std::string extensions(static_cast<size_t>(num_extensions), '\0');
        xrGetVulkanDeviceExtensionsKHR(extensions.size(), &num_extensions, extensions.data());
        return split_string(extensions);
    }
    VkPhysicalDevice physical_device(VkInstance vk_instance)
    {
        XrVulkanGraphicsDeviceGetInfoKHR info{
            .type = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR,
            .systemId = m_system,
            .vulkanInstance = vk_instance
        };

        XrResult result;
        if (!XR_SUCCEEDED(result = xrGetVulkanGraphicsDevice2KHR(&info, &m_physical_device)))
        {
            LOGE("xrGetVulkanGraphicsDevice2KHR failed");
            return VK_NULL_HANDLE;
        }
        m_vk_instance = vk_instance;
        return m_physical_device;
    }
    bool start_session(VkDevice vk_device, uint32_t queue_family_index, uint32_t queue_index)
    {
        XrGraphicsBindingVulkanKHR binding{
            .type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR,
            .instance = m_vk_instance,
            .physicalDevice = m_physical_device,
            .device = vk_device,
            .queueFamilyIndex = queue_family_index,
            .queueIndex = queue_index
        };
        XrSessionCreateInfo info{
            .type = XR_TYPE_SESSION_CREATE_INFO,
            .next = &binding,
            .systemId = m_system
        };
        XrResult r;
        if (!XR_SUCCEEDED(r = xrCreateSession(m_instance, &info, &m_session)))
        {
            LOGE("xrCreateSession failed: %s", to_string(r));
            return false;
        }
        XrReferenceSpaceCreateInfo space_info{
            .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
            .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL,
            .poseInReferenceSpace = XrPosef{
                .orientation = { .x = 0, .y = 0, .z = 0, .w = 1.0 },
                .position = { .x = 0, .y = 0, .z = 0 }
            }
        };
        if (!XR_SUCCEEDED(r = xrCreateReferenceSpace(m_session, &space_info, &m_space)))
        {
            LOGE("xrCreateReferenceSpace failed: %s", to_string(r));
            return false;
        }
        wait_state(XR_SESSION_STATE_READY);
        XrSessionBeginInfo begin_info{
            .type = XR_TYPE_SESSION_BEGIN_INFO,
            .primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO
        };
        if (!XR_SUCCEEDED(r = xrBeginSession(m_session, &begin_info)))
        {
            LOGE("xrBeginSession failed: %s", to_string(r));
            return false;
        }
        return true;
    }
    [[nodiscard]] std::vector<VkFormat> enumerate_swapchain_formats() const noexcept
    {
        uint32_t count = 0;
        xrEnumerateSwapchainFormats(m_session, 0, &count, nullptr);
        std::vector<VkFormat> formats(count);
        xrEnumerateSwapchainFormats(m_session, count, &count, reinterpret_cast<int64_t*>(formats.data()));
        return formats;
    }
};
struct Session
{

};

}
