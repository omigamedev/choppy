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
#include <functional>
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
class Context
{
    XrInstance m_instance = XR_NULL_HANDLE;
    XrSystemId m_system = 0;
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
    std::vector<XrViewConfigurationView> view_configs;
#ifdef __ANDROID__
    jobject m_android_context = nullptr;
    JavaVM* m_android_vm = nullptr;
#endif
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
    [[nodiscard]] std::vector<std::string> split_string(
        std::string_view str, char delimiter = ' ') const noexcept
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
    //
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
        const XrLoaderInitInfoAndroidKHR loader_init{
            .type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR,
            .applicationVM = m_android_vm,
            .applicationContext = m_android_context,
        };
        const auto loader_init_ptr =
            reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loader_init);
        if (XrResult result = xrInitializeLoaderKHR(loader_init_ptr);
            !XR_SUCCEEDED(result))
        {
            LOGE("xrInitializeLoaderKHR failed: %s", to_string(result));
            return false;
        }
#endif

        const std::vector<XrExtensionProperties> xr_instance_extentions = []{
            uint32_t count = 0;
            xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
            std::vector ext_props(count,
                XrExtensionProperties{.type = XR_TYPE_EXTENSION_PROPERTIES});
            xrEnumerateInstanceExtensionProperties(nullptr, count, &count, ext_props.data());
            return ext_props;
        }();
        const std::vector<XrApiLayerProperties> xr_instance_layers = [] {
            uint32_t count = 0;
            xrEnumerateApiLayerProperties(0, &count, nullptr);
            std::vector layers_props(count,
                XrApiLayerProperties{.type = XR_TYPE_API_LAYER_PROPERTIES});
            xrEnumerateApiLayerProperties(count, &count, layers_props.data());
            return layers_props;
        }();

        for (const auto& e : xr_instance_extentions)
            LOGI("XR Ext: %s", e.extensionName);
        for (const auto& p : xr_instance_layers)
            LOGI("XR Api Layer: %s", p.layerName);

        // Create Instance

        const std::vector<const char*> extensions{
            XR_EXT_DEBUG_UTILS_EXTENSION_NAME,
            XR_KHR_VULKAN_ENABLE_EXTENSION_NAME,
            XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
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
        if (XrResult result = xrCreateInstance(&instance_info, &m_instance);
            !XR_SUCCEEDED(result))
        {
            LOGE("xrCreateInstance failed: %s", to_string(result));
            return false;
        }
        XrInstanceProperties instance_props{.type = XR_TYPE_INSTANCE_PROPERTIES};
        if (XrResult result = xrGetInstanceProperties(m_instance, &instance_props);
            !XR_SUCCEEDED(result))
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
        if (XrResult result = xrGetSystemProperties(m_instance, m_system, &system_props);
            !XR_SUCCEEDED(result))
        {
            LOGE("xrGetSystemProperties failed: %s", to_string(result));
            return false;
        }
        LOGI("System: %s", system_props.systemName);

        // Create Vulkan Instance

        volkInitialize();
        const auto instance_extensions = [this] {
            uint32_t count = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
            std::vector<VkExtensionProperties> props(count, VkExtensionProperties{});
            vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data());
            return props;
        }();
        for (const auto& e : instance_extensions)
        {
            LOGI("Vulkan Instance Ext: %s", e.extensionName);
        }
        XrGraphicsRequirementsVulkan2KHR vulkan_req{.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
        if (XrResult result = xrGetVulkanGraphicsRequirements2KHR(&vulkan_req);
            !XR_SUCCEEDED(result))
        {
            LOGE("xrGetVulkanGraphicsRequirements2KHR failed: %s", to_string(result));
            return false;
        }
        LOGI("XR max supported Vulkan: %u.%u.%u",
            XR_VERSION_MAJOR(vulkan_req.maxApiVersionSupported),
            XR_VERSION_MINOR(vulkan_req.maxApiVersionSupported),
            XR_VERSION_PATCH(vulkan_req.maxApiVersionSupported));
        uint32_t vk_req_version = VK_MAKE_VERSION(
            XR_VERSION_MAJOR(vulkan_req.maxApiVersionSupported),
            XR_VERSION_MINOR(vulkan_req.maxApiVersionSupported),
            XR_VERSION_PATCH(vulkan_req.maxApiVersionSupported));
        std::uint32_t vk_runtime_version = 0;
        if (VkResult result = vkEnumerateInstanceVersion(&vk_runtime_version);
            result != VK_SUCCESS)
        {
            LOGE("vkEnumerateInstanceVersion failed: %s", to_string(result));
            return false;
        }
        LOGI("Device runtime Vulkan: %u.%u.%u",
            VK_VERSION_MAJOR(vk_runtime_version),
            VK_VERSION_MINOR(vk_runtime_version),
            VK_VERSION_PATCH(vk_runtime_version));
        uint32_t vk_version = std::max(vk_req_version, vk_runtime_version);
        const VkApplicationInfo app_info{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "choppy_engine",
            .applicationVersion = 0,
            .apiVersion = VK_API_VERSION_1_3
        };
        const std::vector<std::string> vk_instance_available_layers = [this]{
            uint32_t count = 0;
            vkEnumerateInstanceLayerProperties(&count, nullptr);
            std::vector<VkLayerProperties> layers(static_cast<size_t>(count));
            vkEnumerateInstanceLayerProperties(&count, layers.data());
            return layers
                | std::views::transform([](const VkLayerProperties& v)->std::string{ return v.layerName; })
                | std::ranges::to<std::vector<std::string>>();
        }();
        std::vector<const char*> vk_instance_layers;
        for (const auto& e : vk_instance_available_layers)
        {
            if (e == "VK_LAYER_KHRONOS_validation")
                vk_instance_layers.push_back(e.c_str());
            LOGI("Vulkan Layer: %s", e.c_str());
        }

        const std::vector<std::string> vk_instance_available_extensions = [this]{
            uint32_t count = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
            std::vector<VkExtensionProperties > extensions(static_cast<size_t>(count));
            vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
            return extensions
               | std::views::transform([](const VkExtensionProperties& v)->std::string{ return v.extensionName; })
               | std::ranges::to<std::vector<std::string>>();
        }();
        const std::vector<std::string> vk_instance_required_extensions = [this]{
            uint32_t num_extensions = 0;
            xrGetVulkanInstanceExtensionsKHR(0, &num_extensions, nullptr);
            std::string extensions(num_extensions, '\0');
            xrGetVulkanInstanceExtensionsKHR(extensions.size(), &num_extensions, extensions.data());
            return split_string(extensions);
        }();
        std::vector<const char*> vk_instance_extensions{
        };
        const std::array vk_instance_optional_extensions{
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
            VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
            VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
        };
        for (const auto& e : vk_instance_optional_extensions)
        {
            if (std::ranges::contains(vk_instance_available_extensions, e))
            {
                vk_instance_extensions.push_back(e);
            }
        }
        for (const auto& e : vk_instance_required_extensions)
        {
            LOGI("Vulkan Instance Required Ext: %s", e.c_str());
            vk_instance_extensions.push_back(e.c_str());
        }
        const VkInstanceCreateInfo vk_instance_info{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &app_info,
            .enabledLayerCount = static_cast<uint32_t>(vk_instance_layers.size()),
            .ppEnabledLayerNames = vk_instance_layers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(vk_instance_extensions.size()),
            .ppEnabledExtensionNames = vk_instance_extensions.data(),
        };
        XrVulkanInstanceCreateInfoKHR vk_create_info{
            .type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR,
            .systemId = m_system,
            .pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(vkGetInstanceProcAddr),
            .vulkanCreateInfo = &vk_instance_info
        };
        VkResult vk_create_result;
        if (XrResult result = xrCreateVulkanInstanceKHR(&vk_create_info, &m_vk_instance, &vk_create_result);
            !XR_SUCCEEDED(result))
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
        if (XrResult result = xrGetVulkanGraphicsDevice2KHR(&info, &m_physical_device);
            !XR_SUCCEEDED(result))
        {
            LOGE("xrGetVulkanGraphicsDevice2KHR failed: %s", to_string(result));
            return false;
        }
        VkPhysicalDeviceProperties2 physical_device_properties{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        vkGetPhysicalDeviceProperties2(m_physical_device, &physical_device_properties);
        LOGI("Vulkan device: %s", physical_device_properties.properties.deviceName);
        const auto device_extensions = [this] {
            uint32_t count = 0;
            vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &count, nullptr);
            std::vector<VkExtensionProperties> props(count, VkExtensionProperties{});
            vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &count, props.data());
            return props
               | std::views::transform([](const VkExtensionProperties& v)->std::string{ return v.extensionName; })
               | std::ranges::to<std::vector<std::string>>();
        }();
        for (const auto& e : device_extensions)
        {
            LOGI("XR Vulkan Device Ext: %s", e.c_str());
        }

        // Create Device

        const std::vector<VkQueueFamilyProperties2> queue_props = [this]{
            uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties2(m_physical_device, &count, nullptr);
            std::vector<VkQueueFamilyProperties2> props(count,
                {.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2});
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

        const std::vector<std::string> vk_device_required_extensions = [this]{
            uint32_t num_extensions = 0;
            xrGetVulkanDeviceExtensionsKHR(0, &num_extensions, nullptr);
            std::string extensions(static_cast<size_t>(num_extensions), '\0');
            xrGetVulkanDeviceExtensionsKHR(extensions.size(), &num_extensions, extensions.data());
            return split_string(extensions);
        }();
        std::vector<const char*> vk_device_extensions{
            VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        };
        std::array vk_device_optional_extensions{
            VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
            VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
            VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
            VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        };
        for (const auto& e : vk_device_optional_extensions)
        {
            if (std::ranges::contains(device_extensions, e))
            {
                vk_device_extensions.push_back(e);
            }
        }
        for (const auto& e : vk_device_required_extensions)
        {
            LOGI("XR Vulkan Device Required Ext: %s", e.c_str());
            vk_device_extensions.push_back(e.c_str());
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
            .enabledExtensionCount = static_cast<uint32_t>(vk_device_extensions.size()),
            .ppEnabledExtensionNames = vk_device_extensions.data()
        };
        XrVulkanDeviceCreateInfoKHR device_create_info{
            .type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR,
            .systemId = m_system,
            .pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(vkGetInstanceProcAddr),
            .vulkanPhysicalDevice = m_physical_device,
            .vulkanCreateInfo = &device_info
        };
        if (XrResult result = xrCreateVulkanDeviceKHR(&device_create_info, &m_device, &vk_create_result);
            !XR_SUCCEEDED(result))
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
        if (XrResult result = xrCreateSession(m_instance, &info, &m_session);
            !XR_SUCCEEDED(result))
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
        if (XrResult result = xrCreateReferenceSpace(m_session, &space_info, &m_space);
            !XR_SUCCEEDED(result))
        {
            LOGE("xrCreateReferenceSpace failed: %s", to_string(result));
            return false;
        }
        wait_state(XR_SESSION_STATE_READY);
        constexpr XrSessionBeginInfo begin_info{
            .type = XR_TYPE_SESSION_BEGIN_INFO,
            .primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO
        };
        if (XrResult result = xrBeginSession(m_session, &begin_info);
            !XR_SUCCEEDED(result))
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
        const std::array desired_color_formats{
            VK_FORMAT_R8G8B8A8_SRGB,
            VK_FORMAT_B8G8R8A8_SRGB,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_B8G8R8A8_UNORM,
        };
        std::vector<VkFormat> color_formats;
        for (const auto format : supported_color_formats)
        {
            if (std::ranges::contains(desired_color_formats, format))
                color_formats.push_back(format);
        }
        const std::array desired_depth_formats{
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D16_UNORM,
        };
        depth_format = find_format(desired_depth_formats,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        color_format = find_format(color_formats,
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
        view_configs = [this]{
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
            .sampleCount = view_configs[0].recommendedSwapchainSampleCount,
            .width = view_configs[0].recommendedImageRectWidth,
            .height = view_configs[0].recommendedImageRectHeight,
            .faceCount = 1,
            .arraySize = static_cast<uint32_t>(view_configs.size()),
            .mipCount = 1
        };
        if (XrResult result = xrCreateSwapchain(m_session, &color_swapchain_info, &color_swapchain);
            !XR_SUCCEEDED(result))
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
            .sampleCount = view_configs[0].recommendedSwapchainSampleCount,
            .width = view_configs[0].recommendedImageRectWidth,
            .height = view_configs[0].recommendedImageRectHeight,
            .faceCount = 1,
            .arraySize = static_cast<uint32_t>(view_configs.size()),
            .mipCount = 1
        };
        if (XrResult result = xrCreateSwapchain(m_session, &depth_swapchain_info, &depth_swapchain);
            !XR_SUCCEEDED(result))
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
    void present(std::function<void(VkImage color)> render_callback) noexcept
    {
        const XrFrameWaitInfo wait_info{.type = XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState frame_state{.type = XR_TYPE_FRAME_STATE};
        if (const XrResult result = xrWaitFrame(m_session, &wait_info, &frame_state); !XR_SUCCEEDED(result))
        {
            LOGE("xrWaitFrame failed: %s", to_string(result));
            return;
        }
        const XrFrameBeginInfo begin_info{.type = XR_TYPE_FRAME_BEGIN_INFO};
        if (const XrResult result = xrBeginFrame(m_session, &begin_info); !XR_SUCCEEDED(result))
        {
            LOGE("xrBeginFrame failed: %s", to_string(result));
            return;
        }

        std::vector<XrView> views(view_configs.size(), XrView{.type = XR_TYPE_VIEW});
        XrViewState view_state{.type = XR_TYPE_VIEW_STATE};
        XrViewLocateInfo view_locate_info{
            .type = XR_TYPE_VIEW_LOCATE_INFO,
            .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            .displayTime = frame_state.predictedDisplayTime,
            .space = m_space
        };
        uint32_t views_count = 0;
        if (const XrResult result = xrLocateViews(m_session, &view_locate_info, &view_state,
            (uint32_t)views.size(), &views_count, views.data()); !XR_SUCCEEDED(result))
        {
            LOGE("xrLocateViews failed: %s", to_string(result));
            return;
        }

        uint32_t acquired_index = 0;
        XrSwapchainImageAcquireInfo acquire_info{.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if (XrResult result = xrAcquireSwapchainImage(color_swapchain, &acquire_info, &acquired_index); !XR_SUCCEEDED(result))
        {
            LOGE("xrAcquireSwapchainImage failed: %s", to_string(result));
            return;
        }

        XrSwapchainImageWaitInfo image_wait_info{.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_INFINITE_DURATION};
        if (XrResult result = xrWaitSwapchainImage(color_swapchain, &image_wait_info); !XR_SUCCEEDED(result))
        {
            LOGE("xrWaitSwapchainImage failed: %s", to_string(result));
            return;
        }

        render_callback(color_swapchain_images[acquired_index].image);

        std::vector<XrCompositionLayerProjectionView> layer_views(views_count);
        for (uint32_t i = 0; i < views_count; i++)
        {
            layer_views[i] = XrCompositionLayerProjectionView{
                .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
                .pose = views[i].pose,
                .fov = views[i].fov,
                .subImage = XrSwapchainSubImage{
                    .swapchain = color_swapchain,
                    .imageRect = {
                        .offset = {0, 0},
                        .extent = {
                            (int32_t)view_configs[i].recommendedImageRectWidth,
                            (int32_t)view_configs[i].recommendedImageRectHeight
                        }
                    },
                    .imageArrayIndex = i
                }
            };
        }

        XrSwapchainImageReleaseInfo release_info{.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if (XrResult result = xrReleaseSwapchainImage(color_swapchain, &release_info); !XR_SUCCEEDED(result))
        {
            LOGE("xrReleaseSwapchainImage failed: %s", to_string(result));
            return;
        }

        XrCompositionLayerProjection projection_layer{
            .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
            .layerFlags = 0,
            .space = m_space,
            .viewCount = (uint32_t)layer_views.size(),
            .views = layer_views.data(),
        };

        std::vector<XrCompositionLayerBaseHeader*> layers{
            reinterpret_cast<XrCompositionLayerBaseHeader*>(&projection_layer)
        };

        XrFrameEndInfo end_info{
            .type = XR_TYPE_FRAME_END_INFO,
            .displayTime = frame_state.predictedDisplayTime,
            .environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
            .layerCount = (uint32_t)layers.size(),
            .layers = layers.data()
        };
        xrEndFrame(m_session, &end_info);
    }
};
struct Session
{

};

}
