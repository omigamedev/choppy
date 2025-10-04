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

#include <unordered_map>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <functional>
#include <ranges>
#include <thread>
#include <span>
#include <volk.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_to_string.hpp>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <xr_linear.h>

#include <vk_mem_alloc.h>

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

export module ce.xr;
import ce.vk;
import ce.vk.utils;
import ce.xr.utils;
import glm;

export namespace ce::xr
{
enum class TouchControllerButton : uint8_t
{
    Menu, System,
    A, B, X, Y,
    ThumbLeft, ThumbRight,
    EnumCount
};
constexpr auto TouchControllerButtonCount = std::to_underlying(TouchControllerButton::EnumCount);
constexpr std::array TouchControllerButtonToPath {
    std::pair{"button_menu", "/user/hand/left/input/menu/click"},
    std::pair{"button_system", "/user/hand/right/input/system/click"},
    std::pair{"button_a", "/user/hand/right/input/a/click"},
    std::pair{"button_b", "/user/hand/right/input/b/click"},
    std::pair{"button_x", "/user/hand/left/input/x/click"},
    std::pair{"button_y", "/user/hand/left/input/y/click"},
    std::pair{"button_thumb_left", "/user/hand/left/input/thumbstick/click"},
    std::pair{"button_thumb_right", "/user/hand/right/input/thumbstick/click"},
};
struct TouchControllerState final
{
    std::array<bool, TouchControllerButtonCount> buttons{false};
    glm::vec2 thumbstick_left{};
    glm::vec2 thumbstick_right{};
    float trigger_left{0};
    float trigger_right{0};
    float grip_left{0};
    float grip_right{0};
};
class Context final
{
    // openxr
    static constexpr XrPosef m_identity = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    XrInstance m_instance = XR_NULL_HANDLE;
    XrSystemId m_system = XR_NULL_SYSTEM_ID;
    XrSession m_session = XR_NULL_HANDLE;
    XrSessionState m_session_state = XR_SESSION_STATE_UNKNOWN;
    XrSpace m_space = XR_NULL_HANDLE;
    // vulkan
    VmaAllocator m_vma = VK_NULL_HANDLE;
    VkInstance m_vk_instance = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
    VkRenderPass m_renderpass = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkExtent2D m_framebuffer_size{};
    VkViewport m_viewport{};
    const uint32_t m_queue_index = 0;
    uint32_t m_queue_family_index = VK_QUEUE_FAMILY_IGNORED;
    // swapchain resources
    XrSwapchain color_swapchain = XR_NULL_HANDLE;
    XrSwapchain depth_swapchain = XR_NULL_HANDLE;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    std::vector<XrSwapchainImageVulkanKHR> color_swapchain_images;
    std::vector<XrSwapchainImageVulkanKHR> depth_swapchain_images;
    std::vector<VkImageView> color_swapchain_views;
    std::vector<VkImageView> depth_swapchain_views;
    VkImage color_msaa_image = VK_NULL_HANDLE;
    VkImage depth_msaa_image = VK_NULL_HANDLE;
    VkImageView color_msaa_view = VK_NULL_HANDLE;
    VkImageView depth_msaa_view = VK_NULL_HANDLE;
    VmaAllocation color_msaa_allocation = VK_NULL_HANDLE;
    VmaAllocation depth_msaa_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo color_msaa_allocation_info = {};
    VmaAllocationInfo depth_msaa_allocation_info = {};
    std::vector<XrViewConfigurationView> view_configs;
    bool has_msaa_single = false;
    bool has_swapchain_create_info = false;

    std::shared_ptr<vk::Context> m_vk;

    uint32_t m_present_index = 0;
    std::vector<VkCommandBuffer> m_present_cmd;
    VkSemaphore m_timeline_semaphore = VK_NULL_HANDLE;
    uint64_t m_timeline_counter = 0;
    std::vector<uint64_t> m_timeline_values;

    // Input bindings
    XrActionSet m_action_set = XR_NULL_HANDLE;
    std::array<XrAction, TouchControllerButtonCount> m_action_buttons{XR_NULL_HANDLE};
    XrAction m_action_left_thumb = XR_NULL_HANDLE;
    XrAction m_action_right_thumb = XR_NULL_HANDLE;
    XrAction m_action_left_trigger = XR_NULL_HANDLE;
    XrAction m_action_right_trigger = XR_NULL_HANDLE;
    XrAction m_action_left_grip = XR_NULL_HANDLE;
    XrAction m_action_right_grip = XR_NULL_HANDLE;
    XrAction m_action_haptic = XR_NULL_HANDLE;
    XrAction m_action_pose = XR_NULL_HANDLE;
    XrPath m_hands_path[2] = {XR_NULL_PATH, XR_NULL_PATH};
    XrSpace m_hands_space[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
    XrPosef m_hands_pose[2] = {m_identity, m_identity};
    TouchControllerState m_controller{};
#ifdef __ANDROID__
    jobject m_android_context = nullptr;
    JavaVM* m_android_vm = nullptr;
#endif
    [[nodiscard]] static std::vector<VkImage> swapchain_images(
        const std::vector<XrSwapchainImageVulkanKHR>& swapchain) noexcept
    {
        std::vector<VkImage> images;
        std::ranges::transform(swapchain,
            std::back_inserter(images), &XrSwapchainImageVulkan2KHR::image);
        return images;
    }
    void wait_state(const XrSessionState wait_state) const noexcept
    {
        if (wait_state == m_session_state)
            return;
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
    void debug_name(const std::string_view name, const auto obj) const noexcept
    {
        if (vkSetDebugUtilsObjectNameEXT)
        {
            const VkDebugUtilsObjectNameInfoEXT name_info{
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                .objectType = vk::utils::get_type(obj),
                .objectHandle = reinterpret_cast<uint64_t>(obj),
                .pObjectName = name.data(),
            };
            vkSetDebugUtilsObjectNameEXT(m_device, &name_info);
        }
    }
    [[nodiscard]] const char* to_string(const XrResult r) const noexcept
    {
        static char sr[XR_MAX_RESULT_STRING_SIZE]{0};
        xrResultToString(m_instance, r, sr);
        return sr;
    }
    [[nodiscard]] static const char* to_string(const VkResult r) noexcept
    {
        return vk::utils::to_string(r);
    }
    [[nodiscard]] static std::vector<std::string> split_string(
        std::string_view str, char delimiter = ' ') noexcept
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
        VkResult*                                   vulkanResult) const
    {
        static XrFunction<PFN_xrCreateVulkanInstanceKHR> fn{m_instance, "xrCreateVulkanInstanceKHR"};
        return fn.ptr(m_instance, createInfo, vulkanInstance, vulkanResult);
    }
    XRAPI_ATTR XrResult XRAPI_CALL xrCreateVulkanDeviceKHR(
        const XrVulkanDeviceCreateInfoKHR*          createInfo,
        VkDevice*                                   vulkanDevice,
        VkResult*                                   vulkanResult) const
    {
        static XrFunction<PFN_xrCreateVulkanDeviceKHR> fn{m_instance, "xrCreateVulkanDeviceKHR"};
        return fn.ptr(m_instance, createInfo, vulkanDevice, vulkanResult);
    }
    XRAPI_ATTR XrResult XRAPI_CALL xrGetVulkanGraphicsDevice2KHR(
        const XrVulkanGraphicsDeviceGetInfoKHR*     getInfo,
        VkPhysicalDevice*                           vulkanPhysicalDevice) const
    {
        static XrFunction<PFN_xrGetVulkanGraphicsDevice2KHR> fn{m_instance, "xrGetVulkanGraphicsDevice2KHR"};
        return fn.ptr(m_instance, getInfo, vulkanPhysicalDevice);
    }
    XRAPI_ATTR XrResult XRAPI_CALL xrGetVulkanGraphicsRequirements2KHR(
        XrGraphicsRequirementsVulkanKHR*            graphicsRequirements) const
    {
        static XrFunction<PFN_xrGetVulkanGraphicsRequirements2KHR> fn{m_instance, "xrGetVulkanGraphicsRequirements2KHR"};
        return fn.ptr(m_instance, m_system, graphicsRequirements);
    }
    //
    XRAPI_ATTR XrResult XRAPI_CALL xrGetVulkanInstanceExtensionsKHR(
        uint32_t                                    bufferCapacityInput,
        uint32_t*                                   bufferCountOutput,
        char*                                       buffer) const
    {
        static XrFunction<PFN_xrGetVulkanInstanceExtensionsKHR> fn{m_instance, "xrGetVulkanInstanceExtensionsKHR"};
        return fn.ptr(m_instance, m_system, bufferCapacityInput, bufferCountOutput, buffer);
    }
    XRAPI_ATTR XrResult XRAPI_CALL xrGetVulkanDeviceExtensionsKHR(
        uint32_t                                    bufferCapacityInput,
        uint32_t*                                   bufferCountOutput,
        char*                                       buffer) const
    {
        static XrFunction<PFN_xrGetVulkanDeviceExtensionsKHR> fn{m_instance, "xrGetVulkanDeviceExtensionsKHR"};
        return fn.ptr(m_instance, m_system, bufferCapacityInput, bufferCountOutput, buffer);
    }
    XRAPI_ATTR XrResult XRAPI_CALL xrPerfSettingsSetPerformanceLevelEXT(
        XrPerfSettingsDomainEXT                     domain,
        XrPerfSettingsLevelEXT                      level)
    {
        static XrFunction<PFN_xrPerfSettingsSetPerformanceLevelEXT> fn{m_instance, "xrPerfSettingsSetPerformanceLevelEXT"};
        return fn.ptr(m_session, domain, level);
    }
    XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateDisplayRefreshRatesFB(
        uint32_t                                    displayRefreshRateCapacityInput,
        uint32_t*                                   displayRefreshRateCountOutput,
        float*                                      displayRefreshRates)
    {
        static XrFunction<PFN_xrEnumerateDisplayRefreshRatesFB> fn{m_instance, "xrEnumerateDisplayRefreshRatesFB"};
        return fn.ptr(m_session, displayRefreshRateCapacityInput, displayRefreshRateCountOutput, displayRefreshRates);
    }
    XRAPI_ATTR XrResult XRAPI_CALL xrRequestDisplayRefreshRateFB(
        float                                       displayRefreshRate)
    {
        static XrFunction<PFN_xrRequestDisplayRefreshRateFB> fn{m_instance, "xrRequestDisplayRefreshRateFB"};
        return fn.ptr(m_session, displayRefreshRate);
    }

    XRAPI_ATTR static XrResult XRAPI_CALL xrInitializeLoaderKHR(
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
    [[nodiscard]] uint32_t queue_index() const noexcept { return m_queue_index; }
    [[nodiscard]] VkRenderPass renderpass() const noexcept { return m_renderpass; }
    [[nodiscard]] VkFramebuffer framebuffer() const noexcept { return m_framebuffer; }
    [[nodiscard]] VkExtent2D framebuffer_size() const noexcept { return m_framebuffer_size; }
    [[nodiscard]] VkSampleCountFlagBits sample_count() const noexcept
    {
        return VK_SAMPLE_COUNT_4_BIT;
    }
    [[nodiscard]] VkViewport viewport() const noexcept
    {
        return VkViewport{0, 0,
            static_cast<float>(m_framebuffer_size.width),
            static_cast<float>(m_framebuffer_size.height), 0, 1};
    }
    [[nodiscard]] VkRect2D scissor() const noexcept
    {
        return VkRect2D{0, 0, m_framebuffer_size.width, m_framebuffer_size.height};
    }
    [[nodiscard]] uint32_t swapchain_count() const noexcept
    {
        return static_cast<uint32_t>(color_swapchain_images.size());
    }
    [[nodiscard]] std::vector<VkImage> swapchain_color_images() const noexcept
    {
        return swapchain_images(color_swapchain_images);
    }
    [[nodiscard]] std::vector<VkImage> swapchain_depth_images() const noexcept
    {
        return swapchain_images(depth_swapchain_images);
    }
    [[nodiscard]] bool state_active() const noexcept
    {
        return m_session_state == XR_SESSION_STATE_VISIBLE || m_session_state == XR_SESSION_STATE_FOCUSED;
    }
    [[nodiscard]] bool state_focused() const noexcept
    {
        return m_session_state == XR_SESSION_STATE_FOCUSED;
    }
    [[nodiscard]] glm::mat4 hand_pose(const uint32_t hand_index) const noexcept
    {
        XrMatrix4x4f xrm;
        XrMatrix4x4f_CreateFromRigidTransform(&xrm, &m_hands_pose[hand_index]);
        return glm::gtc::make_mat4(xrm.m);
    }
    [[nodiscard]] const TouchControllerState& touch_controllers() const noexcept{ return m_controller; }
#ifdef __ANDROID__
    bool setup_android(JavaVM* vm, jobject context)
    {
        m_android_context = context;
        m_android_vm = vm;
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
        return true;
    }
#endif
    bool create() noexcept
    {
        const std::vector<std::string> xr_instance_extentions = []{
            uint32_t count = 0;
            xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
            std::vector ext_props(count,
                XrExtensionProperties{.type = XR_TYPE_EXTENSION_PROPERTIES});
            xrEnumerateInstanceExtensionProperties(nullptr, count, &count, ext_props.data());
            return ext_props
               | std::views::transform([](const XrExtensionProperties& v)->std::string{ return v.extensionName; })
               | std::ranges::to<std::vector<std::string>>();
        }();
        const std::vector<std::string> xr_instance_layers = [] {
            uint32_t count = 0;
            xrEnumerateApiLayerProperties(0, &count, nullptr);
            std::vector layers_props(count,
                XrApiLayerProperties{.type = XR_TYPE_API_LAYER_PROPERTIES});
            xrEnumerateApiLayerProperties(count, &count, layers_props.data());
            return layers_props
               | std::views::transform([](const XrApiLayerProperties& v)->std::string{ return v.layerName; })
               | std::ranges::to<std::vector<std::string>>();
        }();

         for (const auto& e : xr_instance_extentions)
             LOGI("XR Ext: %s", e.c_str());
         for (const auto& p : xr_instance_layers)
             LOGI("XR Api Layer: %s", p.c_str());

        // Create Instance

        std::vector extensions{
            XR_EXT_DEBUG_UTILS_EXTENSION_NAME,
            XR_KHR_VULKAN_ENABLE_EXTENSION_NAME,
            XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
#ifdef __ANDROID__
            XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME,
            XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,
#endif
        };
        if (std::ranges::contains(xr_instance_extentions, XR_META_VULKAN_SWAPCHAIN_CREATE_INFO_EXTENSION_NAME))
        {
            extensions.push_back(XR_META_VULKAN_SWAPCHAIN_CREATE_INFO_EXTENSION_NAME);
            has_swapchain_create_info = true;
        }
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
        if (const XrResult result = xrGetInstanceProperties(m_instance, &instance_props);
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
        if (const XrResult result = xrGetSystemProperties(m_instance, m_system, &system_props);
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
            std::vector props(count, VkExtensionProperties{});
            vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data());
            return props;
        }();
        for (const auto& e : instance_extensions)
        {
            LOGI("Vulkan Instance Ext: %s", e.extensionName);
        }
        XrGraphicsRequirementsVulkan2KHR vulkan_req{.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
        if (const XrResult result = xrGetVulkanGraphicsRequirements2KHR(&vulkan_req);
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
        if (const VkResult result = vkEnumerateInstanceVersion(&vk_runtime_version);
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
        constexpr VkApplicationInfo app_info{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "choppy_engine",
            .applicationVersion = 0,
            .apiVersion = VK_API_VERSION_1_3
        };
        const std::vector<std::string> vk_instance_available_layers = [this]{
            uint32_t count = 0;
            vkEnumerateInstanceLayerProperties(&count, nullptr);
            std::vector<VkLayerProperties> layers(count);
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
            std::vector<VkExtensionProperties> ext(count);
            vkEnumerateInstanceExtensionProperties(nullptr, &count, ext.data());
            return ext
               | std::views::transform([](const VkExtensionProperties& v)->std::string{ return v.extensionName; })
               | std::ranges::to<std::vector<std::string>>();
        }();
        const std::vector<std::string> vk_instance_required_extensions = [this]{
            uint32_t num_extensions = 0;
            xrGetVulkanInstanceExtensionsKHR(0, &num_extensions, nullptr);
            std::string ext(num_extensions, '\0');
            xrGetVulkanInstanceExtensionsKHR(ext.size(), &num_extensions, ext.data());
            return split_string(ext);
        }();
        std::vector<const char*> vk_instance_extensions{
        };
        constexpr std::array vk_instance_optional_extensions{
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
            .pfnGetInstanceProcAddr = vkGetInstanceProcAddr,
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
            std::vector props(count, VkExtensionProperties{});
            vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &count, props.data());
            return props
               | std::views::transform([](const VkExtensionProperties& v)->std::string{ return v.extensionName; })
               | std::ranges::to<std::vector<std::string>>();
        }();
        // for (const auto& e : device_extensions)
        // {
        //     LOGI("XR Vulkan Device Ext: %s", e.c_str());
        // }

        // Create Device

        const std::vector<VkQueueFamilyProperties2> queue_props = [this]{
            uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties2(m_physical_device, &count, nullptr);
            std::vector props(count, VkQueueFamilyProperties2{.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2});
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
            std::string ext(num_extensions, '\0');
            xrGetVulkanDeviceExtensionsKHR(ext.size(), &num_extensions, ext.data());
            return split_string(ext);
        }();
        std::vector vk_device_extensions{
            VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
            VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
            VK_KHR_MULTIVIEW_EXTENSION_NAME,
            VK_KHR_IMAGELESS_FRAMEBUFFER_EXTENSION_NAME,
            VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
            VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
            VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
            VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
            // VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,
        };
        constexpr std::array vk_device_optional_extensions{
            VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
            VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
            VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
            VK_EXT_PIPELINE_CREATION_CACHE_CONTROL_EXTENSION_NAME,
            VK_EXT_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_EXTENSION_NAME,
        };
        for (const char* e : vk_device_optional_extensions)
        {
            if (std::ranges::contains(device_extensions, e))
            {
                vk_device_extensions.push_back(e);
                LOGI("VK Optional Ext: %s", e);
            }
        }
        for (const std::string& e : vk_device_required_extensions)
        {
            LOGI("VK Xr-Required Ext: %s", e.c_str());
            vk_device_extensions.push_back(e.c_str());
        }
        constexpr std::array queue_priority{1.f};
        const VkDeviceQueueCreateInfo queue_info{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = m_queue_family_index,
            .queueCount = static_cast<uint32_t>(queue_priority.size()),
            .pQueuePriorities = queue_priority.data()
        };
        // Features
        VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT multisampled_feature{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_FEATURES_EXT,
        };
        VkPhysicalDeviceBufferDeviceAddressFeatures bda_feature{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
            .pNext = &multisampled_feature,
        };
        VkPhysicalDevicePipelineCreationCacheControlFeatures pipeline_creation_cache_control_feature{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES,
            .pNext = &bda_feature,
        };
        VkPhysicalDeviceRobustness2FeaturesEXT robustness_feature{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
            .pNext = &pipeline_creation_cache_control_feature,
        };
        VkPhysicalDeviceTimelineSemaphoreFeatures timeline_features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
            .pNext = &robustness_feature,
        };
        VkPhysicalDeviceMultiviewFeatures multiview_feature{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES,
            .pNext = &timeline_features,
        };
        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic_state_features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
            .pNext = &multiview_feature,
        };
        VkPhysicalDeviceImagelessFramebufferFeatures imageless_feature{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES,
            .pNext = &dynamic_state_features,
        };
        VkPhysicalDeviceFeatures2 supported_physical_device_features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &imageless_feature,
        };
        vkGetPhysicalDeviceFeatures2(m_physical_device, &supported_physical_device_features);
        // check required features
        if (!dynamic_state_features.extendedDynamicState)
        {
            LOGE("Failed to find a dynamic state features");
            return false;
        }
        if (!imageless_feature.imagelessFramebuffer)
        {
            LOGE("Failed to find a imageless framebuffer feature");
            return false;
        }
        if (!robustness_feature.nullDescriptor)
        {
            LOGE("Failed to find a null descriptor robustness feature");
            return false;
        }
        if (!multiview_feature.multiview)
        {
            LOGE("Failed to find a multiview feature");
            return false;
        }
        if (!timeline_features.timelineSemaphore)
        {
            LOGE("Failed to find a timeline semaphore feature");
            return false;
        }
        // disable not needed features
        robustness_feature.robustBufferAccess2 = false;
        robustness_feature.robustImageAccess2 = false;
        multiview_feature.multiviewGeometryShader = false;
        multiview_feature.multiviewTessellationShader = false;
        bda_feature.bufferDeviceAddressCaptureReplay = false; // enable for debugging BDA
        bda_feature.bufferDeviceAddressMultiDevice = false;
        has_msaa_single = multisampled_feature.multisampledRenderToSingleSampled;
        // enable supported features
        // TODO: these features should be checked, will this collide with multiview_feature?
        VkPhysicalDeviceVulkan11Features enable_features11{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
            .pNext = &imageless_feature,
            .multiview = true,
            .multiviewGeometryShader = false,
            .multiviewTessellationShader = false,
            .shaderDrawParameters = true,
        };
        const VkPhysicalDeviceFeatures2 enabled_features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &enable_features11,
            .features = {
                .multiDrawIndirect = true
            }
        };
        const VkDeviceCreateInfo device_info{
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &enabled_features,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queue_info,
            .enabledExtensionCount = static_cast<uint32_t>(vk_device_extensions.size()),
            .ppEnabledExtensionNames = vk_device_extensions.data(),
        };
        const XrVulkanDeviceCreateInfoKHR device_create_info{
            .type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR,
            .systemId = m_system,
            .pfnGetInstanceProcAddr = vkGetInstanceProcAddr,
            .vulkanPhysicalDevice = m_physical_device,
            .vulkanCreateInfo = &device_info
        };
        if (const XrResult result = xrCreateVulkanDeviceKHR(&device_create_info, &m_device, &vk_create_result);
            !XR_SUCCEEDED(result))
        {
            LOGE("xrCreateVulkanDeviceKHR failed: %s", to_string(result));
            LOGE("vkCreateDevice failed: %s", to_string(vk_create_result));
            return false;
        }
        volkLoadDevice(m_device);

        // Init VMA
        VmaVulkanFunctions vulkanFunctions = {};
        vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        VmaAllocatorCreateInfo allocatorCreateInfo = {};
        allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_3;
        allocatorCreateInfo.physicalDevice = m_physical_device;
        allocatorCreateInfo.device = m_device;
        allocatorCreateInfo.instance = m_vk_instance;
        allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;
        if (vmaCreateAllocator(&allocatorCreateInfo, &m_vma) != VK_SUCCESS)
        {
            LOGE("vmaCreateAllocator failed");
            return false;
        }

        return true;
    }
    bool create_vulkan_objects(const std::shared_ptr<vk::Context>& vk) noexcept
    {
        m_vk = vk;
        return true;
    }
    bool create_session() noexcept
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
        if (const XrResult result = xrCreateSession(m_instance, &info, &m_session);
            !XR_SUCCEEDED(result))
        {
            LOGE("xrCreateSession failed: %s", to_string(result));
            return false;
        }

#ifdef __ANDROID__
        xrPerfSettingsSetPerformanceLevelEXT(XR_PERF_SETTINGS_DOMAIN_CPU_EXT,
            XR_PERF_SETTINGS_LEVEL_POWER_SAVINGS_EXT);
        xrPerfSettingsSetPerformanceLevelEXT(XR_PERF_SETTINGS_DOMAIN_GPU_EXT,
            XR_PERF_SETTINGS_LEVEL_POWER_SAVINGS_EXT);

        uint32_t count = 0;
        xrEnumerateDisplayRefreshRatesFB(0, &count, nullptr);
        std::vector<float> refresh_rates(count);
        xrEnumerateDisplayRefreshRatesFB(count, &count, refresh_rates.data());
        std::sort(refresh_rates.begin(), refresh_rates.end(), std::greater<>());
        xrRequestDisplayRefreshRateFB(refresh_rates[0]);
#endif

        constexpr XrReferenceSpaceCreateInfo space_info{
            .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
            .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL,
            .poseInReferenceSpace = XrPosef{
                .orientation = { .x = 0, .y = 0, .z = 0, .w = 1.0 },
                .position = { .x = 0, .y = 0, .z = 0 }
            }
        };
        if (const XrResult result = xrCreateReferenceSpace(m_session, &space_info, &m_space);
            !XR_SUCCEEDED(result))
        {
            LOGE("xrCreateReferenceSpace failed: %s", to_string(result));
            return false;
        }
        return true;
    }
    bool begin_session() const noexcept
    {
        wait_state(XR_SESSION_STATE_READY);
        constexpr XrSessionBeginInfo begin_info{
            .type = XR_TYPE_SESSION_BEGIN_INFO,
            .primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO
        };
        if (const XrResult result = xrBeginSession(m_session, &begin_info);
            !XR_SUCCEEDED(result))
        {
            LOGE("xrBeginSession failed: %s", to_string(result));
            return false;
        }
        return true;
    }
    bool end_session() const noexcept
    {
        wait_state(XR_SESSION_STATE_STOPPING);
        if (const XrResult result = xrEndSession(m_session);
            !XR_SUCCEEDED(result))
        {
            LOGE("xrEndSession failed: %s", to_string(result));
            return false;
        }
        return true;
    }
    bool create_msaa_images() noexcept
    {
        // Color
        const VkImageCreateInfo color_msaa_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = color_format,
            .extent = VkExtent3D(m_framebuffer_size.width, m_framebuffer_size.height, 1),
            .mipLevels = 1,
            .arrayLayers = static_cast<uint32_t>(view_configs.size()),
            .samples = VK_SAMPLE_COUNT_4_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        VmaAllocationCreateInfo color_msaa_memory_info{
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        };
        if (const VkResult result = vmaCreateImage(m_vma, &color_msaa_info, &color_msaa_memory_info,
            &color_msaa_image, &color_msaa_allocation, &color_msaa_allocation_info); result != VK_SUCCESS)
        {
            LOGE("vmaCreateImage failed: %s", to_string(result));
            return false;
        }
        debug_name("color_msaa_image", color_msaa_image);

        // Depth
        const VkImageCreateInfo depth_msaa_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = depth_format,
            .extent = VkExtent3D(m_framebuffer_size.width, m_framebuffer_size.height, 1),
            .mipLevels = 1,
            .arrayLayers = static_cast<uint32_t>(view_configs.size()),
            .samples = VK_SAMPLE_COUNT_4_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        VmaAllocationCreateInfo depth_msaa_memory_info{
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        };
        if (const VkResult result = vmaCreateImage(m_vma, &depth_msaa_info, &depth_msaa_memory_info,
            &depth_msaa_image, &depth_msaa_allocation, &depth_msaa_allocation_info); result != VK_SUCCESS)
        {
            LOGE("vmaCreateImage failed: %s", to_string(result));
            return false;
        }
        debug_name("depth_msaa_image", depth_msaa_image);

        // MSAA Views

        // Color
        const VkImageViewCreateInfo color_msaa_view_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = color_msaa_image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            .format = color_format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 2
            }
        };
        if (const VkResult result = vkCreateImageView(m_device, &color_msaa_view_info, nullptr, &color_msaa_view);
            result != VK_SUCCESS)
        {
            LOGE("vkCreateImageView failed: %s", to_string(result));
            return false;
        }
        debug_name("color_msaa_view", color_msaa_view);

        // Depth
        const VkImageViewCreateInfo depth_msaa_view_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = depth_msaa_image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            .format = depth_format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 2
            }
        };
        if (const VkResult result = vkCreateImageView(m_device, &depth_msaa_view_info, nullptr, &depth_msaa_view);
            result != VK_SUCCESS)
        {
            LOGE("vkCreateImageView failed: %s", to_string(result));
            return false;
        }
        debug_name("depth_msaa_view", depth_msaa_view);
        return true;
    }
    bool create_renderpass_msaa_single() noexcept
    {
        const std::array renderpass_attachments{
            VkAttachmentDescription2{
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                .format = color_format,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            },
            VkAttachmentDescription2{
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                .format = depth_format,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            },
        };
        constexpr VkAttachmentReference2 renderpass_sub_color{
            .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
            .attachment = 0,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };
        constexpr VkAttachmentReference2 renderpass_sub_depth{
            .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
            .attachment = 1,
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        };
        const VkSubpassDescriptionDepthStencilResolve multisampled_depth_esolve{
            .sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE,
            .depthResolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT,
            .stencilResolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT,
            //.pDepthStencilResolveAttachment = &renderpass_sub_depth
        };
        const VkMultisampledRenderToSingleSampledInfoEXT multisampled{
            .sType = VK_STRUCTURE_TYPE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_INFO_EXT,
            .pNext = &multisampled_depth_esolve,
            .multisampledRenderToSingleSampledEnable = true,
            .rasterizationSamples = VK_SAMPLE_COUNT_4_BIT
        };
        const std::array renderpass_sub{
            VkSubpassDescription2{
                .sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,
                .pNext = has_msaa_single ? &multisampled : nullptr,
                .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .viewMask = 0b11, // enable multiview rendering
                .colorAttachmentCount = 1,
                .pColorAttachments = &renderpass_sub_color,
                .pDepthStencilAttachment = &renderpass_sub_depth
            },
        };
        const VkRenderPassCreateInfo2 renderpass_info{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
            .attachmentCount = static_cast<uint32_t>(renderpass_attachments.size()),
            .pAttachments = renderpass_attachments.data(),
            .subpassCount = static_cast<uint32_t>(renderpass_sub.size()),
            .pSubpasses = renderpass_sub.data()
        };
        if (const VkResult result = vkCreateRenderPass2(m_device, &renderpass_info, nullptr, &m_renderpass);
            result != VK_SUCCESS)
        {
            LOGE("vkCreateRenderPass failed: %s", to_string(result));
            return false;
        }
        debug_name("ce_renderpass", m_renderpass);
        return true;
    }
    bool create_renderpass_msaa() noexcept
    {
        const std::array renderpass_attachments{
            VkAttachmentDescription2{
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                .format = color_format,
                .samples = VK_SAMPLE_COUNT_4_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            },
            VkAttachmentDescription2{
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                .format = depth_format,
                .samples = VK_SAMPLE_COUNT_4_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            },
            VkAttachmentDescription2{
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                .format = color_format,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            },
        };
        constexpr VkAttachmentReference2 renderpass_sub_color{
            .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
            .attachment = 0,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };
        constexpr VkAttachmentReference2 renderpass_sub_depth{
            .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
            .attachment = 1,
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        };
        constexpr VkAttachmentReference2 renderpass_sub_resolve{
            .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
            .attachment = 2,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };
        const std::array renderpass_sub{
            VkSubpassDescription2{
                .sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,
                .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .viewMask = 0b11, // enable multiview rendering
                .colorAttachmentCount = 1,
                .pColorAttachments = &renderpass_sub_color,
                .pResolveAttachments = &renderpass_sub_resolve,
                .pDepthStencilAttachment = &renderpass_sub_depth,
            },
        };
        const VkRenderPassCreateInfo2 renderpass_info{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
            .attachmentCount = static_cast<uint32_t>(renderpass_attachments.size()),
            .pAttachments = renderpass_attachments.data(),
            .subpassCount = static_cast<uint32_t>(renderpass_sub.size()),
            .pSubpasses = renderpass_sub.data()
        };
        if (const VkResult result = vkCreateRenderPass2(m_device, &renderpass_info, nullptr, &m_renderpass);
            result != VK_SUCCESS)
        {
            LOGE("vkCreateRenderPass failed: %s", to_string(result));
            return false;
        }
        debug_name("ce_renderpass", m_renderpass);
        return true;
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
        constexpr std::array desired_color_formats{
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
        constexpr std::array desired_depth_formats{
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D16_UNORM,
        };
        depth_format = vk::utils::find_format(m_physical_device, desired_depth_formats,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        color_format = vk::utils::find_format(m_physical_device, color_formats,
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
        view_configs = [this]{
            uint32_t count = 0;
            xrEnumerateViewConfigurationViews(m_instance, m_system,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr);
            std::vector views(count, XrViewConfigurationView{.type = XR_TYPE_VIEW_CONFIGURATION_VIEW});
            xrEnumerateViewConfigurationViews(m_instance, m_system,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, static_cast<uint32_t>(views.size()),
                &count, views.data());
            return views;
        }();
        m_framebuffer_size.width = view_configs[0].recommendedImageRectWidth;
        m_framebuffer_size.height = view_configs[0].recommendedImageRectHeight;

        // Color swapchain

        constexpr XrVulkanSwapchainCreateInfoMETA color_swapchain_info_meta{
            .type = XR_TYPE_VULKAN_SWAPCHAIN_CREATE_INFO_META,
            .additionalCreateFlags = VK_IMAGE_CREATE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT
        };
        const XrSwapchainCreateInfo color_swapchain_info{
            .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
            .next = has_swapchain_create_info ? &color_swapchain_info_meta : nullptr,
            .usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
            .format = color_format,
            .sampleCount = view_configs[0].recommendedSwapchainSampleCount,
            .width = m_framebuffer_size.width,
            .height = m_framebuffer_size.height,
            .faceCount = 1,
            .arraySize = static_cast<uint32_t>(view_configs.size()),
            .mipCount = 1
        };
        if (const XrResult result = xrCreateSwapchain(m_session, &color_swapchain_info, &color_swapchain);
            !XR_SUCCEEDED(result))
        {
            LOGE("xrCreateSwapchain failed: %s", to_string(result));
            return false;
        }
        color_swapchain_images = [this] {
            uint32_t count = 0;
            xrEnumerateSwapchainImages(color_swapchain, 0, &count, nullptr);
            std::vector images(count, XrSwapchainImageVulkanKHR{.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
            xrEnumerateSwapchainImages(color_swapchain, count, &count,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
            return images;
        }();
        for (uint32_t i = 0; i < color_swapchain_images.size(); ++i)
        {
            debug_name(std::format("swapchain_color[{}]", i), color_swapchain_images[i].image);
        }

        // Depth swapchain

        const XrSwapchainCreateInfo depth_swapchain_info{
            .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
            .next = has_swapchain_create_info ? &color_swapchain_info_meta : nullptr,
            .usageFlags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .format = depth_format,
            .sampleCount = view_configs[0].recommendedSwapchainSampleCount,
            .width = m_framebuffer_size.width,
            .height = m_framebuffer_size.height,
            .faceCount = 1,
            .arraySize = static_cast<uint32_t>(view_configs.size()),
            .mipCount = 1
        };
        if (const XrResult result = xrCreateSwapchain(m_session, &depth_swapchain_info, &depth_swapchain);
            !XR_SUCCEEDED(result))
        {
            LOGE("xrCreateSwapchain failed: %s", to_string(result));
            return false;
        }
        depth_swapchain_images = [this] {
            uint32_t count = 0;
            xrEnumerateSwapchainImages(depth_swapchain, 0, &count, nullptr);
            std::vector images(count, XrSwapchainImageVulkanKHR{.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
            xrEnumerateSwapchainImages(depth_swapchain, count, &count,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
            return images;
        }();

        // Views

        color_swapchain_views.resize(color_swapchain_images.size());
        depth_swapchain_views.resize(color_swapchain_images.size());
        for (uint32_t i = 0; i < color_swapchain_images.size(); i++)
        {
            // Color
            const VkImageViewCreateInfo color_info{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = color_swapchain_images[i].image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                .format = color_format,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 2
                }
            };
            if (const VkResult result = vkCreateImageView(m_device, &color_info, nullptr, &color_swapchain_views[i]);
                result != VK_SUCCESS)
            {
                LOGE("vkCreateImageView failed: %s", to_string(result));
                return false;
            }
            debug_name(std::format("swapchain_color_view[{}]", i), color_swapchain_views[i]);

            // Depth
            const VkImageViewCreateInfo depth_info{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = depth_swapchain_images[i].image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                .format = depth_format,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 2
                }
            };
            if (const VkResult result = vkCreateImageView(m_device, &depth_info, nullptr, &depth_swapchain_views[i]);
                result != VK_SUCCESS)
            {
                LOGE("vkCreateImageView failed: %s", to_string(result));
                return false;
            }
            debug_name(std::format("swapchain_depth_view[{}]", i), depth_swapchain_views[i]);
        }

        if (!has_msaa_single)
        {
            create_msaa_images();
        }

        // Renderpass

        if (has_msaa_single)
        {
            if (!create_renderpass_msaa_single())
            {
                LOGE("create_renderpass_msaa_single failed");
                return false;
            }
        }
        else
        {
            if (!create_renderpass_msaa())
            {
                LOGE("create_renderpass_msaa failed");
                return false;
            }
        }

        // Framebuffer

        const VkImageUsageFlags xr_usage = has_msaa_single ? VkImageUsageFlags{} : VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
        // constexpr VkImageUsageFlags xr_usage{};
        std::vector attachment_images_info{
            VkFramebufferAttachmentImageInfo{
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO,
                .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .width = m_framebuffer_size.width,
                .height = m_framebuffer_size.height,
                .layerCount = 2,
                .viewFormatCount = 1,
                .pViewFormats = &color_format,
            },
            VkFramebufferAttachmentImageInfo{
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO,
                .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                .width = m_framebuffer_size.width,
                .height = m_framebuffer_size.height,
                .layerCount = 2,
                .viewFormatCount = 1,
                .pViewFormats = &depth_format,
            }
        };
        if (!has_msaa_single)
        {
            attachment_images_info.push_back({
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO,
                .usage = xr_usage | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .width = m_framebuffer_size.width,
                .height = m_framebuffer_size.height,
                .layerCount = 2,
                .viewFormatCount = 1,
                .pViewFormats = &color_format,
            });
        }
        const VkFramebufferAttachmentsCreateInfo attachments_info {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO,
            .attachmentImageInfoCount = static_cast<uint32_t>(attachment_images_info.size()),
            .pAttachmentImageInfos = attachment_images_info.data(),
        };
        const VkFramebufferCreateInfo framebufferInfo{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext = &attachments_info,                     // Attachments info here
            .flags = VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT,  // Key flag
            .renderPass = m_renderpass,
            .attachmentCount = static_cast<uint32_t>(attachment_images_info.size()),
            .width = m_framebuffer_size.width,
            .height = m_framebuffer_size.height,
            .layers = 1, // must be one if multiview is used
        };
        if (const VkResult result = vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_framebuffer);
            result != VK_SUCCESS)
        {
            LOGE("vkCreateFramebuffer failed: %s", to_string(result));
            return false;
        }
        debug_name("ce_framebuffer", m_framebuffer);

        m_present_cmd = m_vk->create_command_buffers("render_frame", swapchain_count());
        m_timeline_semaphore = m_vk->create_timeline_semaphore();
        m_timeline_values = std::vector<uint64_t>(swapchain_count(), 0);

        return true;
    }
    void init_resources(VkCommandBuffer cmd) const noexcept
    {
        for (VkImage img : swapchain_depth_images())
        {
            constexpr VkImageSubresourceRange subresource_range = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 2};
            const VkImageMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                    | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .image = img,
                .subresourceRange = subresource_range
            };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
        if (!has_msaa_single)
        {
            const std::array barriers{
                VkImageMemoryBarrier{
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .srcAccessMask = 0,
                    .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .image = color_msaa_image,
                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 2}
                },
                VkImageMemoryBarrier{
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .srcAccessMask = 0,
                    .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                        | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    .image = depth_msaa_image,
                    .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 2}
                },
            };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
        }
    }
    [[nodiscard]] XrActionSet create_action_set(const std::string_view name) const noexcept
    {
        XrActionSetCreateInfo action_set_info{XR_TYPE_ACTION_SET_CREATE_INFO};
        std::ranges::copy(name, action_set_info.actionSetName);
        std::ranges::copy(name, action_set_info.localizedActionSetName);
        action_set_info.priority = 0;
        XrActionSet action_set = XR_NULL_HANDLE;
        if (const XrResult result = xrCreateActionSet(m_instance, &action_set_info, &action_set);
            result != XR_SUCCESS)
        {
            LOGE("xrCreateActionSet failed: %s", to_string(result));
            return XR_NULL_HANDLE;
        }
        return action_set;
    }
    [[nodiscard]] XrSpace create_action_space(XrAction action, const XrPath hand) const noexcept
    {
        const XrActionSpaceCreateInfo info{
            .type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
            .action = action,
            .subactionPath = hand,
            .poseInActionSpace = m_identity,
        };
        XrSpace space = XR_NULL_HANDLE;
        if (const XrResult result = xrCreateActionSpace(m_session, &info, &space);
            result != XR_SUCCESS)
        {
            LOGE("xrCreateActionSpace failed: %s", to_string(result));
            return XR_NULL_HANDLE;
        }
        return space;
    }
    [[nodiscard]] XrAction create_action(const std::string_view name, XrActionType type,
        const std::span<const XrPath> subactions = {}) const noexcept
    {
        XrActionCreateInfo action_info{XR_TYPE_ACTION_CREATE_INFO};
        std::ranges::copy(name, action_info.actionName);
        std::ranges::copy(name, action_info.localizedActionName);
        action_info.actionType = type;
        action_info.countSubactionPaths = subactions.size();
        action_info.subactionPaths = subactions.data();
        XrAction action = XR_NULL_HANDLE;
        if (const XrResult result = xrCreateAction(m_action_set, &action_info, &action);
            result != XR_SUCCESS)
        {
            LOGE("xrCreateAction failed: %s", to_string(result));
            return XR_NULL_HANDLE;
        }
        return action;
    }
    [[nodiscard]] XrPath create_path(const std::string& path) const noexcept
    {
        XrPath xr_path = XR_NULL_PATH;
        if (const XrResult result = xrStringToPath(m_instance, path.c_str(), &xr_path);
            result != XR_SUCCESS)
        {
            LOGE("xrStringToPath failed: %s", to_string(result));
            return XR_NULL_PATH;
        }
        return xr_path;
    }
    bool bind_input() noexcept
    {
        m_action_set = create_action_set("gameplay");
        m_action_left_thumb = create_action("left_thumb", XR_ACTION_TYPE_VECTOR2F_INPUT);
        m_action_right_thumb = create_action("right_thumb", XR_ACTION_TYPE_VECTOR2F_INPUT);
        m_action_left_trigger = create_action("left_trigger", XR_ACTION_TYPE_FLOAT_INPUT);
        m_action_right_trigger = create_action("right_trigger", XR_ACTION_TYPE_FLOAT_INPUT);
        m_action_left_grip = create_action("left_grip", XR_ACTION_TYPE_FLOAT_INPUT);
        m_action_right_grip = create_action("right_grip", XR_ACTION_TYPE_FLOAT_INPUT);
        m_hands_path[0] = create_path("/user/hand/left");
        m_hands_path[1] = create_path("/user/hand/right");
        m_action_haptic = create_action("vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT, m_hands_path);
        m_action_pose = create_action("hand_pose", XR_ACTION_TYPE_POSE_INPUT, m_hands_path);
        m_hands_space[0] = create_action_space(m_action_pose, m_hands_path[0]);
        m_hands_space[1] = create_action_space(m_action_pose, m_hands_path[1]);

        std::vector bindings{
            XrActionSuggestedBinding{m_action_left_thumb, create_path("/user/hand/left/input/thumbstick")},
            XrActionSuggestedBinding{m_action_right_thumb, create_path("/user/hand/right/input/thumbstick")},
            XrActionSuggestedBinding{m_action_left_trigger, create_path("/user/hand/left/input/trigger/value")},
            XrActionSuggestedBinding{m_action_right_trigger, create_path("/user/hand/right/input/trigger/value")},
            XrActionSuggestedBinding{m_action_left_grip, create_path("/user/hand/left/input/squeeze/value")},
            XrActionSuggestedBinding{m_action_right_grip, create_path("/user/hand/right/input/squeeze/value")},
            XrActionSuggestedBinding{m_action_haptic, create_path("/user/hand/right/output/haptic")},
            XrActionSuggestedBinding{m_action_pose, create_path("/user/hand/left/input/aim/pose")},
            XrActionSuggestedBinding{m_action_pose, create_path("/user/hand/right/input/aim/pose")},
            XrActionSuggestedBinding{m_action_pose, create_path("/user/hand/right/input/aim/pose")},
        };
        for (size_t i = 0; i < TouchControllerButtonToPath.size(); ++i)
        {
            m_action_buttons[i] = create_action(TouchControllerButtonToPath[i].first, XR_ACTION_TYPE_BOOLEAN_INPUT);
            bindings.emplace_back(m_action_buttons[i], create_path(TouchControllerButtonToPath[i].second));
        }

        XrInteractionProfileSuggestedBinding suggestedBindings{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggestedBindings.interactionProfile = create_path("/interaction_profiles/oculus/touch_controller");
        suggestedBindings.countSuggestedBindings = bindings.size();
        suggestedBindings.suggestedBindings = bindings.data();
        if (const XrResult result = xrSuggestInteractionProfileBindings(m_instance, &suggestedBindings);
            result != XR_SUCCESS)
        {
            LOGE("xrSuggestInteractionProfileBinding failed: %s", to_string(result));
            return false;
        }

        XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attachInfo.countActionSets = 1;
        attachInfo.actionSets = &m_action_set;
        if (const XrResult result = xrAttachSessionActionSets(m_session, &attachInfo);
            result != XR_SUCCESS)
        {
            LOGE("xrAttachSessionActionSets failed: %s", to_string(result));
            return false;
        }

        return true;
    }
    bool sync_action_set(const XrActionSet set) const noexcept
    {
        const XrActiveActionSet activeActionSet{set, XR_NULL_PATH};
        const XrActionsSyncInfo syncInfo{
            .type = XR_TYPE_ACTIONS_SYNC_INFO,
            .countActiveActionSets = 1,
            .activeActionSets = &activeActionSet,
        };
        if (const XrResult result = xrSyncActions(m_session, &syncInfo);
            result != XR_SUCCESS)
        {
            LOGE("xrSyncActions failed: %s", to_string(result));
            return false;
        }
        return true;
    }
    [[nodiscard]] std::optional<XrActionStateBoolean> action_state_boolean(XrAction action) const noexcept
    {
        XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
        XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
        info.action = action;
        if (const XrResult result = xrGetActionStateBoolean(m_session, &info, &state);
            result != XR_SUCCESS)
        {
            LOGE("xrGetActionStateBoolean failed: %s", to_string(result));
            return std::nullopt;
        }
        return state;
    }
    [[nodiscard]] std::optional<XrActionStateFloat> action_state_float(XrAction action) const noexcept
    {
        XrActionStateFloat state{XR_TYPE_ACTION_STATE_BOOLEAN};
        XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
        info.action = action;
        if (const XrResult result = xrGetActionStateFloat(m_session, &info, &state);
            result != XR_SUCCESS)
        {
            LOGE("xrGetActionStateFloat failed: %s", to_string(result));
            return std::nullopt;
        }
        return state;
    }
    [[nodiscard]] std::optional<XrActionStateVector2f> action_state_vec2(XrAction action) const noexcept
    {
        XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
        XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
        info.action = action;
        if (const XrResult result = xrGetActionStateVector2f(m_session, &info, &state);
            result != XR_SUCCESS)
        {
            LOGE("xrGetActionStateVector2f failed: %s", to_string(result));
            return std::nullopt;
        }
        return state;
    }
    [[nodiscard]] std::optional<XrPosef> action_state_pose(XrAction action, const XrPath hand_path,
        XrSpace space, const XrTime time) const noexcept
    {
        XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
        XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
        info.action = action;
        info.subactionPath = hand_path;
        if (const XrResult result = xrGetActionStatePose(m_session, &info, &state);
            result != XR_SUCCESS)
        {
            LOGE("xrGetActionStatePose failed: %s", to_string(result));
            return std::nullopt;
        }
        if (state.isActive)
        {
            XrSpaceLocation spaceLocation{XR_TYPE_SPACE_LOCATION};
            if (const XrResult result = xrLocateSpace(space, m_space, time, &spaceLocation);
                result != XR_SUCCESS)
            {
                LOGE("xrLocateSpace failed: %s", to_string(result));
                return std::nullopt;
            }
            const bool position_valid = spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT;
            const bool orientation_valid = spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            if (position_valid && orientation_valid)
            {
                return spaceLocation.pose;
            }
        }
        return std::nullopt;
    }
    bool apply_haptic_feedback(XrAction action, const XrPath hand_path) const noexcept
    {
        XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
        vibration.amplitude = 0.5;
        vibration.duration = XR_MIN_HAPTIC_DURATION;
        vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
        XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
        info.action = action;
        info.subactionPath = hand_path;
        const auto header = reinterpret_cast<const XrHapticBaseHeader*>(&vibration);
        if (const XrResult result = xrApplyHapticFeedback(m_session, &info, header);
            result != XR_SUCCESS)
        {
            LOGE("xrApplyHapticFeedback failed: %s", to_string(result));
            return false;
        }
        return true;
    }
    bool sync_input() noexcept
    {
        ZoneScoped;
        sync_action_set(m_action_set);

        // NOT CORRECT TouchControllerButtonCount may not match m_action_buttons on other devices no Touch
        for (size_t i = 0; i < TouchControllerButtonCount; ++i)
        {
            const auto button_state = action_state_boolean(m_action_buttons[i]);
            if (button_state && button_state->changedSinceLastSync)
            {
                m_controller.buttons[i] = button_state->currentState;
            }
        }

        return true;
    }
    bool sync_pose(const XrTime time) noexcept
    {
        ZoneScoped;
        // hands
        for (uint32_t i = 0; i < 2; ++i)
        {
            if (const auto pose = action_state_pose(m_action_pose, m_hands_path[i], m_hands_space[i], time))
                m_hands_pose[i] = pose.value();
        }
        if (const auto thumb = action_state_vec2(m_action_left_thumb))
        {
            if (thumb.value().isActive)
            {
                m_controller.thumbstick_left.x = thumb.value().currentState.x;
                m_controller.thumbstick_left.y = thumb.value().currentState.y;
            }
        }
        if (const auto thumb = action_state_vec2(m_action_right_thumb))
        {
            if (thumb.value().isActive)
            {
                m_controller.thumbstick_right.x = thumb.value().currentState.x;
                m_controller.thumbstick_right.y = thumb.value().currentState.y;
            }
        }
        if (const auto trigger = action_state_float(m_action_left_trigger))
        {
            if (trigger.value().isActive)
            {
                m_controller.trigger_left = trigger.value().currentState;
            }
        }
        if (const auto trigger = action_state_float(m_action_right_trigger))
        {
            if (trigger.value().isActive)
            {
                m_controller.trigger_right = trigger.value().currentState;
            }
        }
        if (const auto grip = action_state_float(m_action_left_grip))
        {
            if (grip.value().isActive)
            {
                m_controller.grip_left = grip.value().currentState;
            }
        }
        if (const auto grip = action_state_float(m_action_right_grip))
        {
            if (grip.value().isActive)
            {
                m_controller.grip_right = grip.value().currentState;
            }
        }
        return true;
    }
    void handle_state_change(const XrEventDataSessionStateChanged* e) noexcept
    {
        switch (e->state) {
        case XR_SESSION_STATE_IDLE:
            break;
        case XR_SESSION_STATE_READY:
            break;
        case XR_SESSION_STATE_SYNCHRONIZED:
            break;
        case XR_SESSION_STATE_VISIBLE:
            break;
        case XR_SESSION_STATE_FOCUSED:
            break;
        case XR_SESSION_STATE_STOPPING:
            break;
        case XR_SESSION_STATE_LOSS_PENDING:
            break;
        case XR_SESSION_STATE_EXITING:
            break;
        default:
            break;
        }
    }
    void poll_events() noexcept
    {
        XrEventDataBuffer event_data{.type = XR_TYPE_EVENT_DATA_BUFFER};
        while (xrPollEvent(m_instance, &event_data) == XR_SUCCESS)
        {
            if (event_data.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
            {
                const auto* e = reinterpret_cast<const XrEventDataSessionStateChanged*>(&event_data);
                LOGI("OpenXR session state changed to: %s", utils::to_string(e->state));
                m_session_state = e->state;
                handle_state_change(e);
            }
            event_data.type = XR_TYPE_EVENT_DATA_BUFFER; // Reset for the next poll
        }
    }
    [[nodiscard]] std::optional<uint64_t> timeline_value() const noexcept
    {
        uint64_t value = 0;
        if (const VkResult result = vkGetSemaphoreCounterValue(m_device, m_timeline_semaphore, &value);
            result != VK_SUCCESS)
        {
            LOGE("timeline semaphore value failed: %s", vk::utils::to_string(result));
            return std::nullopt;
        }
        return value;
    }
    void present(const std::function<void(const vk::utils::FrameContext& frame)>& update_callback,
        const std::function<void(const vk::utils::FrameContext& frame)>& render_callback) noexcept
    {
        ZoneScoped;

        {
            ZoneScopedN("wait semaphores");
            ZoneValue(m_timeline_values[m_present_index]);
            const VkSemaphoreWaitInfo timeline_wait_info{
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                .semaphoreCount = 1,
                .pSemaphores = &m_timeline_semaphore,
                .pValues = &m_timeline_values[m_present_index],
            };
            vkWaitSemaphores(m_device, &timeline_wait_info, UINT64_MAX);
        }
        vkResetCommandBuffer(m_present_cmd[m_present_index], 0);

        m_timeline_values[m_present_index] = ++m_timeline_counter;
        ZoneValue(m_timeline_values[m_present_index]);

        XrFrameState frame_state{.type = XR_TYPE_FRAME_STATE};
        {
            ZoneScopedN("xrWaitFrame");
            constexpr XrFrameWaitInfo wait_info{.type = XR_TYPE_FRAME_WAIT_INFO};
            if (const XrResult result = xrWaitFrame(m_session, &wait_info, &frame_state); !XR_SUCCEEDED(result))
            {
                LOGE("xrWaitFrame failed: %s", to_string(result));
                return;
            }
        }

        {
            ZoneScopedN("xrBeginFrame");
            constexpr XrFrameBeginInfo begin_info{.type = XR_TYPE_FRAME_BEGIN_INFO};
            if (const XrResult result = xrBeginFrame(m_session, &begin_info); !XR_SUCCEEDED(result))
            {
                LOGE("xrBeginFrame failed: %s", to_string(result));
                return;
            }
        }

        sync_pose(frame_state.predictedDisplayTime);

        std::vector views(view_configs.size(), XrView{.type = XR_TYPE_VIEW});
        XrViewState view_state{.type = XR_TYPE_VIEW_STATE};
        const XrViewLocateInfo view_locate_info{
            .type = XR_TYPE_VIEW_LOCATE_INFO,
            .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            .displayTime = frame_state.predictedDisplayTime,
            .space = m_space
        };
        uint32_t views_count = 0;
        if (const XrResult result = xrLocateViews(m_session, &view_locate_info, &view_state,
            static_cast<uint32_t>(views.size()), &views_count, views.data()); !XR_SUCCEEDED(result))
        {
            LOGE("xrLocateViews failed: %s", to_string(result));
            return;
        }

        uint32_t acquired_index = 0;
        {
            ZoneScopedN("acquire image");
            constexpr XrSwapchainImageAcquireInfo acquire_info{.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            if (const XrResult result = xrAcquireSwapchainImage(color_swapchain, &acquire_info, &acquired_index);
                !XR_SUCCEEDED(result))
            {
                LOGE("xrAcquireSwapchainImage failed: %s", to_string(result));
                return;
            }
        }

        {
            ZoneScopedN("wait image");
            constexpr XrSwapchainImageWaitInfo image_wait_info{
                .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_INFINITE_DURATION};
            if (const XrResult result = xrWaitSwapchainImage(color_swapchain, &image_wait_info); !XR_SUCCEEDED(result))
            {
                LOGE("xrWaitSwapchainImage failed: %s", to_string(result));
                return;
            }
        }

        vk::utils::FrameContext frame{
            .cmd = VK_NULL_HANDLE,
            .size = m_framebuffer_size,
            .color_image = has_msaa_single ? color_swapchain_images[acquired_index].image : color_msaa_image,
            .depth_image = has_msaa_single ? depth_swapchain_images[acquired_index].image : depth_msaa_image,
            .resolve_color_image = has_msaa_single ? VK_NULL_HANDLE : color_swapchain_images[acquired_index].image,
            .color_view = has_msaa_single ? color_swapchain_views[acquired_index] : color_msaa_view,
            .depth_view = has_msaa_single ? depth_swapchain_views[acquired_index] : depth_msaa_view,
            .resolve_color_view = has_msaa_single ? VK_NULL_HANDLE : color_swapchain_views[acquired_index],
            .framebuffer = m_framebuffer,
            .renderpass = m_renderpass,
            .present_index = m_present_index,
            .timeline_value = m_timeline_counter,
            .display_time = frame_state.predictedDisplayTime,
        };

        for (uint32_t i = 0; i < views_count; i++)
        {
            XrMatrix4x4f projection_matrix;
            XrMatrix4x4f_CreateProjectionFov(&projection_matrix,
                GRAPHICS_VULKAN, views[i].fov, 0.1f, 100.f);
            frame.projection[i] = glm::gtc::make_mat4(projection_matrix.m);

            XrMatrix4x4f viewMatrix;
            XrMatrix4x4f viewMatrixInv;
            XrMatrix4x4f_CreateFromRigidTransform(&viewMatrix, &views[i].pose); // Pose to Matrix
            XrMatrix4x4f_Invert(&viewMatrixInv, &viewMatrix);             // Invert to get View Matrix

            const glm::mat4 cam_t = glm::gtx::translate(glm::gtc::make_vec3(reinterpret_cast<float*>(&views[i].pose.position)));
            const glm::quat cam_q = glm::gtc::make_quat(reinterpret_cast<float*>(&views[i].pose.orientation));
            frame.view[i] = glm::gtc::make_mat4(viewMatrixInv.m);//glm::inverse(cam_t * glm::gtc::mat4_cast(cam_q));
            frame.view_pos[i] = glm::gtc::make_vec3(reinterpret_cast<float*>(&views[i].pose.position));
            frame.view_quat[i] = cam_q;
        }

        if (frame_state.shouldRender)
        {
            update_callback(frame);
        }

        constexpr VkCommandBufferBeginInfo cmd_begin_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };
        vkBeginCommandBuffer(m_present_cmd[m_present_index], &cmd_begin_info);
        if (frame_state.shouldRender)
        {
            frame.cmd = m_present_cmd[m_present_index];
            render_callback(frame);
        }
        vkEndCommandBuffer(m_present_cmd[m_present_index]);

        {
            ZoneScopedN("submit");
            const uint64_t signal_values = m_timeline_values[m_present_index];
            const VkTimelineSemaphoreSubmitInfo timeline_submit_info{
                .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
                .signalSemaphoreValueCount = 1,
                .pSignalSemaphoreValues = &signal_values,
            };
            const VkSubmitInfo submit_info{
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .pNext = &timeline_submit_info,
                .commandBufferCount = 1,
                .pCommandBuffers = &m_present_cmd[m_present_index],
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &m_timeline_semaphore,
            };
            if (const VkResult result = vkQueueSubmit(m_vk->queue(), 1, &submit_info, VK_NULL_HANDLE);
                result != VK_SUCCESS)
            {
                LOGE("Failed to submit");
            }
        }

        {
            ZoneScopedN("release swapchain");
            constexpr XrSwapchainImageReleaseInfo release_info{.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            if (const XrResult result = xrReleaseSwapchainImage(color_swapchain, &release_info); !XR_SUCCEEDED(result))
            {
                LOGE("xrReleaseSwapchainImage failed: %s", to_string(result));
                return;
            }
        }

        {
            ZoneScopedN("end frame");
            std::vector layer_views(views_count, XrCompositionLayerProjectionView{});
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
                                static_cast<int32_t>(m_framebuffer_size.width),
                                static_cast<int32_t>(m_framebuffer_size.height)
                            }
                        },
                        .imageArrayIndex = i
                    }
                };
            }

            const XrCompositionLayerProjection projection_layer{
                .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
                .layerFlags = 0,
                .space = m_space,
                .viewCount = static_cast<uint32_t>(layer_views.size()),
                .views = layer_views.data(),
            };

            const std::array layers{
                reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection_layer)
            };

            const XrFrameEndInfo end_info{
                .type = XR_TYPE_FRAME_END_INFO,
                .displayTime = frame_state.predictedDisplayTime,
                .environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
                .layerCount = static_cast<uint32_t>(layers.size()),
                .layers = layers.data()
            };
            xrEndFrame(m_session, &end_info);
            FrameMarkNamed("xrEndFrame");
        }
        m_present_index = (m_present_index + 1) % swapchain_count();
    }
};
struct Session
{

};
/*
class Controller
{
public:
    enum class Hand : uint8_t {Left, Right};
    enum class ActionType : uint8_t {Value, Touch, X, Y, Click, Pose, Vibration};
    enum class Source : uint8_t
    {
        Squeeze, Trigger, Thumbstick, Thumbrest,
        LeftX, LeftY, LeftMenu,
        RightA, RightB, RightSystemMenu
    };
    struct Binding
    {
        XrPath path = XR_NULL_PATH;
        XrAction action = XR_NULL_HANDLE;
    };
private:
    XrInstance m_instance;
    XrActionSet m_action_set;
    Hand m_hand;
    XrPath m_hand_path = XR_NULL_PATH;
    [[nodiscard]] const char* hand_to_string(const Hand hand) const noexcept
    {
        switch (hand) {
        case Hand::Left: return "left";
        case Hand::Right: return "right";
        default: return "unknown";
        }
    }
    [[nodiscard]] const char* source_to_string(const Source button) const noexcept
    {
        switch (button) {
        case Source::Squeeze: return "squeeze";
        case Source::Trigger: return "trigger";
        case Source::Thumbstick: return "thumbstick";
        case Source::Thumbrest: return "thumbrest";
        case Source::LeftX: return "x";
        case Source::LeftY: return "y";
        case Source::LeftMenu: return "menu";
        case Source::RightA: return "a";
        case Source::RightB: return "b";
        case Source::RightSystemMenu: return "system";
        }
    }
    [[nodiscard]] const char* action_type_to_string(const ActionType type) const noexcept
    {
        switch (type) {
        case ActionType::Value: return "value";
        case ActionType::Touch: return "touch";
        case ActionType::X: return "x";
        case ActionType::Y: return "y";
        case ActionType::Click: return "click";
        case ActionType::Pose: return "pose";
        case ActionType::Vibration: return "haptic";
        }
        return "unknown";
    }
    [[nodiscard]] XrActionType action_type_to_xr(const ActionType type) const noexcept
    {
        switch (type) {
        case ActionType::Value: return XR_ACTION_TYPE_FLOAT_INPUT;
        case ActionType::Touch: return XR_ACTION_TYPE_BOOLEAN_INPUT;
        case ActionType::X: return XR_ACTION_TYPE_FLOAT_INPUT;
        case ActionType::Y: return XR_ACTION_TYPE_FLOAT_INPUT;
        case ActionType::Click: return XR_ACTION_TYPE_BOOLEAN_INPUT;
        case ActionType::Pose: return XR_ACTION_TYPE_POSE_INPUT;
        case ActionType::Vibration: return XR_ACTION_TYPE_VIBRATION_OUTPUT;
        }
        return XR_ACTION_TYPE_MAX_ENUM;
    }
public:
    Controller(XrInstance instance, XrActionSet action_set, const Hand hand) noexcept
        : m_instance(instance), m_action_set(action_set), m_hand(hand) {}
    bool create_hand() noexcept
    {
        const auto path_string = std::format("/user/hand/{}", hand_to_string(m_hand));
        if (const XrResult result = xrStringToPath(m_instance, path_string.c_str(), &m_hand_path);
            result != XR_SUCCESS)
        {
            LOGE("xrStringToPath failed: %s", utils::to_string(result));
            return false;
        }
        return true;
    }
    std::optional<Binding> create_binding(const std::string_view action_name,
        const Source source, const ActionType action_type) noexcept
    {
        Binding binding;
        const auto path_string = std::format("/user/hand/{}/input/{}/{}",
            hand_to_string(m_hand), source_to_string(source), action_type_to_string(action_type));
        if (const XrResult result = xrStringToPath(m_instance, path_string.c_str(),
            &binding.path); result != XR_SUCCESS)
        {
            LOGE("xrStringToPath failed: %s", to_string(result));
            return std::nullopt;
        }
        XrActionCreateInfo action_info{XR_TYPE_ACTION_CREATE_INFO};
        std::ranges::copy(action_name, action_info.actionName);
        std::ranges::copy(action_name, action_info.localizedActionName);
        action_info.actionType = action_type_to_xr(action_type);
        if (const XrResult result = xrCreateAction(m_action_set, &action_info, &binding.action);
            result != XR_SUCCESS)
        {
            LOGE("xrCreateAction failed: %s", to_string(result));
            return std::nullopt;
        }
        return binding;
    }
    bool bind_button(const std::string_view name, const Source source, const ActionType type,
        const std::function<void(Source source, ActionType type)>& callback) noexcept
    {
        const auto binding_opt = create_binding(name, source, type);
        if (!binding_opt)
        {
            LOGE("bind_button failed");
            return false;
        }
    }
};
*/
}
