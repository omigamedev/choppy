module;
#include <string>
#include <utility>
#include <vector>
#include <array>
#include <format>
#include <functional>
#include <string_view>
#include <span>
#include <ranges>

#include <volk.h>
#include <vk_mem_alloc.h>

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#include <vulkan/vulkan_android.h>
#elifdef _WIN32
#define LOGE(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
// #include <vulkan/vulkan_win32.h>
#endif

export module ce.vk;
import ce.vk.utils;
import ce.platform;
import ce.platform.globals;
import glm;

#ifdef _WIN32
import ce.platform.win32;
#elifdef __ANDROID__
import ce.platform.android;
#endif

export namespace ce::vk
{
struct NoCopy
{
    NoCopy() = default;
    NoCopy(const NoCopy& other) = delete;
    NoCopy(NoCopy&& other) noexcept = default;
    NoCopy& operator=(const NoCopy& other) = delete;
    NoCopy& operator=(NoCopy&& other) noexcept = default;
};

class Context final
{
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkRenderPass m_renderpass = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkCommandPool m_cmd_pool = VK_NULL_HANDLE;
    VkCommandPool m_cmd_pool_imm = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
    VkPhysicalDeviceLimits m_physical_device_limits = {};

    std::vector<VkImage> m_color_swapchain_images;
    std::vector<VkImageView> m_color_swapchain_views;
    VkImage m_color_image = VK_NULL_HANDLE;
    VkImageView m_color_view = VK_NULL_HANDLE;
    VkImage m_depth_image = VK_NULL_HANDLE;
    VkImageView m_depth_view = VK_NULL_HANDLE;
    VmaAllocation m_color_allocation = VK_NULL_HANDLE;
    VmaAllocation m_depth_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo m_color_allocation_info = {};
    VmaAllocationInfo m_depth_allocation_info = {};

    bool m_msaa_enabled = true;

    std::vector<VkSemaphore> m_wait_swapchain;
    std::vector<VkSemaphore> m_wait_render;
    uint32_t m_present_index = 0;
    std::vector<VkCommandBuffer> m_present_cmd;
    VkSemaphore m_timeline_semaphore = VK_NULL_HANDLE;
    uint64_t m_timeline_counter = 0;
    std::vector<uint64_t> m_timeline_values;

    std::shared_ptr<platform::Window> m_window;
    uint32_t m_queue_family_index = std::numeric_limits<uint32_t>::max();
    VkFormat m_color_format = VK_FORMAT_UNDEFINED;
    VkFormat m_depth_format = VK_FORMAT_UNDEFINED;
    const uint32_t m_queue_index = 0;
    VmaAllocator m_vma = VK_NULL_HANDLE;

    VkCommandBuffer tracy_cmd = VK_NULL_HANDLE;
    TracyVkCtx tracy_ctx = nullptr;

    bool create_vulkan_objects() noexcept
    {
        VkPhysicalDeviceProperties2 physical_device_properties{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        vkGetPhysicalDeviceProperties2(m_physical_device, &physical_device_properties);
        m_physical_device_limits = physical_device_properties.properties.limits;
        LOGI("Vulkan device: %s", physical_device_properties.properties.deviceName);

        const VkDeviceQueueInfo2 get_queue_info{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
            .queueFamilyIndex = m_queue_family_index,
            .queueIndex = m_queue_index
        };
        vkGetDeviceQueue2(m_device, &get_queue_info, &m_queue);
        if (m_queue == VK_NULL_HANDLE)
        {
            LOGE("Failed to get device queue info");
            return false;
        }
        debug_name("ce_queue", m_queue);

        // Immediate command pool
        const VkCommandPoolCreateInfo pool_imm_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .queueFamilyIndex = m_queue_family_index
        };
        if (const VkResult result = vkCreateCommandPool(m_device, &pool_imm_info, nullptr, &m_cmd_pool_imm);
            result != VK_SUCCESS)
        {
            LOGE("Failed to create command pool");
            return false;
        }
        debug_name("ce_command_pool_imm", m_cmd_pool_imm);

        // Resettable command queue
        const VkCommandPoolCreateInfo pool_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = m_queue_family_index
        };
        if (const VkResult result = vkCreateCommandPool(m_device, &pool_info, nullptr, &m_cmd_pool);
            result != VK_SUCCESS)
        {
            LOGE("Failed to create command pool");
            return false;
        }
        debug_name("ce_command_pool", m_cmd_pool);

        tracy_cmd = create_command_buffer("tracy_cmd");
        tracy_ctx = TracyVkContextCalibrated(m_physical_device, m_device, m_queue, tracy_cmd,
            vkGetPhysicalDeviceCalibrateableTimeDomainsEXT, vkGetCalibratedTimestampsEXT);

        // Init VMA
        VmaVulkanFunctions vulkanFunctions = {};
        vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        VmaAllocatorCreateInfo allocatorCreateInfo = {};
        allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_3;
        allocatorCreateInfo.physicalDevice = m_physical_device;
        allocatorCreateInfo.device = m_device;
        allocatorCreateInfo.instance = m_instance;
        allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;
        if (vmaCreateAllocator(&allocatorCreateInfo, &m_vma) != VK_SUCCESS)
        {
            LOGE("vmaCreateAllocator failed");
            return false;
        }
        return true;
    }
public:
    [[nodiscard]] VkInstance instance() const noexcept { return m_instance; }
    [[nodiscard]] VkDevice device() const noexcept { return m_device; }
    [[nodiscard]] uint32_t queue_index() const noexcept { return m_queue_index; }
    [[nodiscard]] uint32_t queue_family_index() const noexcept { return m_queue_family_index; }
    [[nodiscard]] VmaAllocator vma() const noexcept { return m_vma; }
    [[nodiscard]] VkQueue queue() const noexcept { return m_queue; }
    [[nodiscard]] VkRenderPass renderpass() const noexcept { return m_renderpass; }
    [[nodiscard]] VkSwapchainKHR swapchain() const noexcept { return m_swapchain; }
    [[nodiscard]] VkSampleCountFlagBits samples_count() const noexcept { return m_msaa_enabled ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT; }
    [[nodiscard]] TracyVkCtx tracy() const noexcept { return tracy_ctx; }
    [[nodiscard]] uint32_t swapchain_count() const noexcept
    {
        return static_cast<uint32_t>(m_color_swapchain_images.size());
    }
    [[nodiscard]] const VkPhysicalDeviceLimits& physical_device_limits() const noexcept
    {
        return m_physical_device_limits;
    }
    [[nodiscard]] VkViewport viewport() const noexcept
    {
        const auto [width, height] = m_window->size();
        return VkViewport{0, 0,
            static_cast<float>(width),
            static_cast<float>(height), 0, 1};
    }
    [[nodiscard]] VkRect2D scissor() const noexcept
    {
        const auto [width, height] = m_window->size();
        return VkRect2D{0, 0, width, height};
    }
    bool create_from(VkInstance instance, VkDevice device,
        VkPhysicalDevice physical_device, uint32_t queue_family_index) noexcept
    {
        m_instance = instance;
        m_device = device;
        debug_name("ce_instance", m_instance);
        debug_name("ce_device", m_device);
        m_physical_device = physical_device;
        debug_name("ce_physical_device", m_physical_device);
        m_queue_family_index = queue_family_index;
        return create_vulkan_objects();
    }
    bool create(const std::shared_ptr<platform::Window>& window) noexcept
    {
        volkInitialize();
        std::uint32_t vk_version = 0;
        vkEnumerateInstanceVersion(&vk_version);
        LOGI("Found Vulkan runtime %d.%d.%d",
             VK_API_VERSION_MAJOR(vk_version),
             VK_API_VERSION_MINOR(vk_version),
             VK_API_VERSION_PATCH(vk_version));
        const VkApplicationInfo app_info{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "choppy_engine",
            .applicationVersion = 0,
            .apiVersion = vk_version
        };
        const std::vector<std::string> vk_instance_available_extensions = [this]{
            uint32_t count = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
            std::vector<VkExtensionProperties> ext(count);
            vkEnumerateInstanceExtensionProperties(nullptr, &count, ext.data());
            return ext
               | std::views::transform([](const VkExtensionProperties& v)->std::string{ return v.extensionName; })
               | std::ranges::to<std::vector<std::string>>();
        }();
        const std::vector<const char*> layers{
            "VK_LAYER_KHRONOS_validation"
        };
        std::vector<const char*> extensions{
            VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef __ANDROID__
            VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
#else
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#endif
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
                extensions.push_back(e);
            }
        }
        const auto instance_info = VkInstanceCreateInfo{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &app_info,
            .enabledLayerCount = static_cast<uint32_t>(layers.size()),
            .ppEnabledLayerNames = layers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data()
        };
        if (VkResult result = vkCreateInstance(&instance_info, nullptr, &m_instance); result != VK_SUCCESS)
        {
            LOGE("Failed to create Vulkan instance: %s", utils::to_string(result));
            return false;
        }
        volkLoadInstance(m_instance);

        // Find Physical Device
#ifdef _WIN32
        auto& win32 = platform::GetPlatform<platform::Win32>();
        auto win32_window = std::static_pointer_cast<platform::Win32Window>(window);
        const VkWin32SurfaceCreateInfoKHR surface_info{
            .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
            .hinstance = win32.hinstance(),
            .hwnd = win32_window->hwnd(),
        };
        if (const VkResult result = vkCreateWin32SurfaceKHR(m_instance, &surface_info, nullptr, &m_surface);
            result != VK_SUCCESS)
        {
            LOGE("Failed to create Vulkan surface: %s", utils::to_string(result));
            return false;
        }
#else
        LOGE("Android windows not implemented, yet :)");
        return false;
#endif

        // TODO: find a suitable device
        const std::vector<VkPhysicalDevice> physical_devices = [this]{
            uint32_t count = 0;
            vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
            std::vector<VkPhysicalDevice> devices(count);
            vkEnumeratePhysicalDevices(m_instance, &count, devices.data());
            auto discrete_devices = devices
                | std::views::filter([](const VkPhysicalDevice& device)
                {
                    VkPhysicalDeviceProperties properties{};
                    vkGetPhysicalDeviceProperties(device, &properties);
                    return properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
                });
            if (!discrete_devices.empty())
                return discrete_devices | std::ranges::to<std::vector>();
            return devices | std::ranges::to<std::vector>();
        }();

        for (auto& physical_device : physical_devices)
        {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(physical_device, &properties);
            const auto queue_family_properties = [physical_device]{
                uint32_t count = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
                std::vector<VkQueueFamilyProperties> queue_families(count);
                vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, queue_families.data());
                return queue_families
                    | std::views::filter([](const VkQueueFamilyProperties& p)
                    {
                        return p.queueFlags & VK_QUEUE_GRAPHICS_BIT;
                    })
                    | std::ranges::to<std::vector>();
            }();
            bool found_queue_family = false;
            for (size_t i = 0; i < queue_family_properties.size(); ++i)
            {
                VkBool32 supports_surface = false;
                if (const VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(
                    physical_device, i, m_surface, &supports_surface); result != VK_SUCCESS)
                {
                    LOGE("Failed to get Vulkan surface support: %s", utils::to_string(result));
                    return false;
                }
                if (supports_surface)
                {
                    m_queue_family_index = i;
                    m_physical_device = physical_device;
                    found_queue_family = true;
                    break;
                }
            }
            if (found_queue_family)
            {
                LOGI("Found device: %s and queue family index %d", properties.deviceName, m_queue_family_index);
                break;
            }
        }

        VkPhysicalDeviceProperties2 physical_device_properties{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
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

        // Create Device
        std::vector vk_device_extensions{
            VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
            //VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
            VK_KHR_IMAGELESS_FRAMEBUFFER_EXTENSION_NAME,
            VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
            //VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
            VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,
            VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
        };
        constexpr std::array vk_device_optional_extensions{
            VK_KHR_MULTIVIEW_EXTENSION_NAME,
            VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
            VK_EXT_PIPELINE_CREATION_CACHE_CONTROL_EXTENSION_NAME,
            VK_EXT_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_EXTENSION_NAME,
            VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME,
        };
        for (const char* e : vk_device_optional_extensions)
        {
            if (std::ranges::contains(device_extensions, e))
            {
                vk_device_extensions.push_back(e);
                LOGI("VK Optional Ext: %s", e);
            }
        }
        // Features
        VkPhysicalDeviceBufferDeviceAddressFeatures bda_feature{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
        };
        VkPhysicalDeviceTimelineSemaphoreFeatures timeline_features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
            //.pNext = &bda_feature,
        };
        VkPhysicalDeviceRobustness2FeaturesEXT robustness_feature{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
            .pNext = &timeline_features,
        };
        VkPhysicalDeviceMultiviewFeatures multiview_feature{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES,
            .pNext = &robustness_feature,
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
        if (!timeline_features.timelineSemaphore)
        {
            LOGE("Failed to find a timeline semaphore feature");
            return false;
        }
        // disable not needed features
        robustness_feature.robustBufferAccess2 = false;
        robustness_feature.robustImageAccess2 = false;
        bda_feature.bufferDeviceAddressCaptureReplay = false; // enable for debugging BDA
        bda_feature.bufferDeviceAddressMultiDevice = false;
        // enable supported features
        const VkPhysicalDeviceFeatures2 enabled_features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &imageless_feature,
            .features = {
                .multiDrawIndirect = true,
            }
        };
        constexpr std::array queue_priority{1.f};
        const VkDeviceQueueCreateInfo queue_info{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = m_queue_family_index,
            .queueCount = static_cast<uint32_t>(queue_priority.size()),
            .pQueuePriorities = queue_priority.data()
        };
        const VkDeviceCreateInfo device_info{
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &enabled_features,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queue_info,
            .enabledExtensionCount = static_cast<uint32_t>(vk_device_extensions.size()),
            .ppEnabledExtensionNames = vk_device_extensions.data(),
        };
        if (VkResult result = vkCreateDevice(m_physical_device, &device_info, nullptr, &m_device);
            result != VK_SUCCESS)
        {
            LOGE("Failed to create Vulkan device: %s", utils::to_string(result));
            return false;
        }
        volkLoadDevice(m_device);
        debug_name("ce_device", m_device);
        debug_name("ce_instance", m_instance);
        debug_name("ce_physical_device", m_physical_device);
        m_window = window;

        return create_vulkan_objects();
    }
    bool create_swapchain() noexcept
    {
        constexpr std::array desired_color_formats{
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_B8G8R8A8_SRGB,
            VK_FORMAT_R8G8B8A8_SRGB,
        };
        constexpr std::array desired_depth_formats{
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D16_UNORM,
        };
        m_depth_format = utils::find_format(m_physical_device, desired_depth_formats,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        m_color_format = utils::find_format(m_physical_device, desired_color_formats,
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
        const auto [width, height] = m_window->size();
        const VkSwapchainCreateInfoKHR create_info{
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = m_surface,
            .minImageCount = 3,
            .imageFormat = m_color_format,
            .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
            .imageExtent = VkExtent2D{width, height},
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = VK_PRESENT_MODE_FIFO_KHR,
            .clipped = true,
            .oldSwapchain = VK_NULL_HANDLE,
        };
        if (const VkResult result = vkCreateSwapchainKHR(m_device, &create_info, nullptr, &m_swapchain);
            result != VK_SUCCESS)
        {
            LOGE("Failed to create Vulkan swapchain: %s", utils::to_string(result));
            return false;
        }
        m_color_swapchain_images = [this]{
            uint32_t count = 0;
            vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, nullptr);
            std::vector<VkImage> images(count);
            vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, images.data());
            return images;
        }();
        m_color_swapchain_views.reserve(m_color_swapchain_images.size());
        for (VkImage image : m_color_swapchain_images)
        {
            debug_name("color_swapchain", image);
            VkImageView& view = m_color_swapchain_views.emplace_back();
            // Color
            const VkImageViewCreateInfo color_create_info{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = image,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = m_color_format,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };
            if (const VkResult result = vkCreateImageView(m_device, &color_create_info, nullptr, &view);
                result != VK_SUCCESS)
            {
                LOGE("Failed to create Vulkan image view: %s", utils::to_string(result));
                return false;
            }
            debug_name("color_swapchain_view", view);
        }
        // Multisampled color image
        VkImageCreateInfo color_image_create_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = m_color_format,
            .extent = VkExtent3D{width, height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = samples_count(),
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        VmaAllocationCreateInfo color_memory_info{
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        };
        if (const VkResult result = vmaCreateImage(m_vma, &color_image_create_info, &color_memory_info,
            &m_color_image, &m_color_allocation, &m_color_allocation_info); result != VK_SUCCESS)
        {
            LOGE("vmaCreateImage failed: %s", utils::to_string(result));
            return false;
        }
        debug_name("color_msaa_image", m_color_image);
        const VkImageViewCreateInfo color_view_create_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_color_image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = m_color_format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        if (const VkResult result = vkCreateImageView(m_device, &color_view_create_info, nullptr, &m_color_view);
            result != VK_SUCCESS)
        {
            LOGE("Failed to create Vulkan image view: %s", utils::to_string(result));
            return false;
        }
        debug_name("color_msaa_view", m_color_view);
        // Multisampled depth image
        VkImageCreateInfo depth_image_create_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = m_depth_format,
            .extent = VkExtent3D{width, height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = samples_count(),
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        VmaAllocationCreateInfo depth_memory_info{
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        };
        if (const VkResult result = vmaCreateImage(m_vma, &depth_image_create_info, &depth_memory_info,
            &m_depth_image, &m_depth_allocation, &m_depth_allocation_info); result != VK_SUCCESS)
        {
            LOGE("vmaCreateImage failed: %s", utils::to_string(result));
            return false;
        }
        debug_name("depth_msaa_image", m_depth_image);
        const VkImageViewCreateInfo depth_view_create_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_depth_image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = m_depth_format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        if (const VkResult result = vkCreateImageView(m_device, &depth_view_create_info, nullptr, &m_depth_view);
            result != VK_SUCCESS)
        {
            LOGE("Failed to create Vulkan image view: %s", utils::to_string(result));
            return false;
        }
        debug_name("depth_msaa_view", m_depth_view);
        for (size_t i = 0; i < m_color_swapchain_images.size(); ++i)
        {
            constexpr VkSemaphoreCreateInfo semaphore_create_info{
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            };
            vkCreateSemaphore(m_device, &semaphore_create_info, nullptr, &m_wait_render.emplace_back());
            debug_name(std::format("render_sem[{}]", i), m_wait_render.back());
            vkCreateSemaphore(m_device, &semaphore_create_info, nullptr, &m_wait_swapchain.emplace_back());
            debug_name(std::format("swapchain_sem[{}]", i), m_wait_swapchain.back());
        }

        m_present_cmd = create_command_buffers("render_frame", swapchain_count());
        //m_present_fences = create_fences("present_fence", swapchain_count(), VK_FENCE_CREATE_SIGNALED_BIT);
        m_timeline_semaphore = create_timeline_semaphore();
        m_timeline_values = std::vector<uint64_t>(swapchain_count(), 0);
        return true;
    }
    [[nodiscard]] VkSemaphore create_timeline_semaphore() const noexcept
    {
        constexpr VkSemaphoreTypeCreateInfo semaphore_type_create_info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = 0
        };
        const VkSemaphoreCreateInfo semaphore_create_info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &semaphore_type_create_info
        };
        VkSemaphore semaphore{VK_NULL_HANDLE};
        if (const VkResult result = vkCreateSemaphore(m_device, &semaphore_create_info, nullptr, &semaphore);
            result != VK_SUCCESS)
        {
            LOGE("Failed to create timeline semaphore: %s", utils::to_string(result));
            return VK_NULL_HANDLE;
        }
        return semaphore;
    }
    [[nodiscard]] VkCommandBuffer create_command_buffer(const std::string& name) const noexcept
    {
        const VkCommandBufferAllocateInfo cmd_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = m_cmd_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (const VkResult result = vkAllocateCommandBuffers(m_device, &cmd_info, &cmd); result != VK_SUCCESS)
        {
            LOGE("Failed to allocate command buffer");
            return VK_NULL_HANDLE;
        }
        debug_name(name, cmd);
        return cmd;
    }
    [[nodiscard]] std::vector<VkCommandBuffer> create_command_buffers(
        const std::string& name, const uint32_t count) const noexcept
    {
        const VkCommandBufferAllocateInfo cmd_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = m_cmd_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = count
        };
        std::vector<VkCommandBuffer> cmds(count);
        if (const VkResult result = vkAllocateCommandBuffers(m_device, &cmd_info, cmds.data()); result != VK_SUCCESS)
        {
            LOGE("Failed to allocate command buffer");
            return {};
        }
        for (uint32_t i = 0; i < count; i++)
        {
            debug_name(std::format("{}[{}]", name, i), cmds[i]);
        }
        return cmds;
    }
    [[nodiscard]] VkFence create_fence(const std::string& name, const VkFenceCreateFlags flags = {}) const noexcept
    {
        VkFence fence = VK_NULL_HANDLE;
        const VkFenceCreateInfo fence_info{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = flags
        };
        if (const VkResult result = vkCreateFence(m_device, &fence_info, nullptr, &fence); result != VK_SUCCESS)
        {
            LOGE("Failed to create fence");
            return VK_NULL_HANDLE;
        }
        debug_name(name, fence);
        return fence;
    }
    [[nodiscard]] std::vector<VkFence> create_fences(const std::string& name,
        const uint32_t count, const VkFenceCreateFlags flags = {}) const noexcept
    {
        std::vector<VkFence> fences(count, VK_NULL_HANDLE);
        const VkFenceCreateInfo fence_info{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = flags
        };
        for (uint32_t i = 0; i < count; i++)
        {
            if (const VkResult result = vkCreateFence(m_device, &fence_info, nullptr, &fences[i]); result != VK_SUCCESS)
            {
                LOGE("Failed to create fence %d", i);
                continue;
            }
            debug_name(std::format("{}[{}]", name, i), fences[i]);
        }
        return fences;
    }
    void exec_immediate(std::string_view name, const std::function<void(VkCommandBuffer cmd)>& fn) const noexcept
    {
        VkFence fence = VK_NULL_HANDLE;
        constexpr VkFenceCreateInfo fence_info{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        };
        if (const VkResult result = vkCreateFence(m_device, &fence_info, nullptr, &fence); result != VK_SUCCESS)
        {
            LOGE("Failed to create fence");
            return;
        }
        debug_name(std::format("immediate_{}_fence", name), fence);
        const VkCommandBufferAllocateInfo cmd_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = m_cmd_pool_imm,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (const VkResult result = vkAllocateCommandBuffers(m_device, &cmd_info, &cmd); result != VK_SUCCESS)
        {
            LOGE("Failed to allocate command buffer");
            vkDestroyFence(m_device, fence, nullptr);
            return;
        }
        debug_name(std::format("immediate_{}", name), cmd);
        constexpr VkCommandBufferBeginInfo cmd_begin_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };
        vkBeginCommandBuffer(cmd, &cmd_begin_info);
        fn(cmd);
        vkEndCommandBuffer(cmd);
        const VkSubmitInfo submit_info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd
        };
        vkQueueSubmit(m_queue, 1, &submit_info, fence);
        if (const VkResult result = vkWaitForFences(m_device, 1, &fence, true, std::numeric_limits<uint64_t>::max());
            result != VK_SUCCESS)
        {
            LOGE("Failed to wait for fence");
        }
        vkFreeCommandBuffers(m_device, m_cmd_pool_imm, 1, &cmd);
        vkDestroyFence(m_device, fence, nullptr);
    }
    void submit(VkCommandBuffer cmd, VkFence fence = VK_NULL_HANDLE, VkSemaphore wait = VK_NULL_HANDLE,
        VkPipelineStageFlags wait_stage = {}, VkSemaphore signal = VK_NULL_HANDLE) const noexcept
    {
        const VkSubmitInfo submit_info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = wait ? 1u : 0u,
            .pWaitSemaphores = &wait,
            .pWaitDstStageMask = &wait_stage,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
            .signalSemaphoreCount = signal ? 1u : 0u,
            .pSignalSemaphores = &signal
        };
        if (const VkResult result = vkQueueSubmit(m_queue, 1, &submit_info, fence);
            result != VK_SUCCESS)
        {
            LOGE("Failed to submit");
        }
    }
    void exec(std::string_view name, const std::function<void(VkCommandBuffer cmd)>& fn) const noexcept
    {
        const VkCommandBufferAllocateInfo cmd_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = m_cmd_pool_imm,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (const VkResult result = vkAllocateCommandBuffers(m_device, &cmd_info, &cmd); result != VK_SUCCESS)
        {
            LOGE("Failed to allocate command buffer");
            return;
        }
        debug_name(std::format("exec_{}", name), cmd);
        constexpr VkCommandBufferBeginInfo cmd_begin_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };
        vkBeginCommandBuffer(cmd, &cmd_begin_info);
        fn(cmd);
        vkEndCommandBuffer(cmd);
        const VkSubmitInfo submit_info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd
        };
        vkQueueSubmit(m_queue, 1, &submit_info, VK_NULL_HANDLE);
    }
    bool create_renderpass() noexcept
    {
        const std::array renderpass_attachments{
            VkAttachmentDescription2{
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                .format = m_color_format,
                .samples = samples_count(),
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            },
            VkAttachmentDescription2{
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                .format = m_depth_format,
                .samples = samples_count(),
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            },
            VkAttachmentDescription2{
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                .format = m_color_format,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
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
            LOGE("vkCreateRenderPass failed: %s", utils::to_string(result));
            return false;
        }
        debug_name("ce_windowedrenderpass", m_renderpass);
        return true;
    }
    bool create_framebuffer() noexcept
    {
        const auto [width, height] = m_window->size();
        const std::array attachment_images_info{
            VkFramebufferAttachmentImageInfo{
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO,
                .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .width = width,
                .height = height,
                .layerCount = 1,
                .viewFormatCount = 1,
                .pViewFormats = &m_color_format,
            },
            VkFramebufferAttachmentImageInfo{
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO,
                .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                .width = width,
                .height = height,
                .layerCount = 1,
                .viewFormatCount = 1,
                .pViewFormats = &m_depth_format,
            },
            VkFramebufferAttachmentImageInfo{
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO,
                .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .width = width,
                .height = height,
                .layerCount = 1,
                .viewFormatCount = 1,
                .pViewFormats = &m_color_format,
            }
        };
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
            .width = width,
            .height = height,
            .layers = 1, // must be one if multiview is used
        };
        if (const VkResult result = vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_framebuffer);
            result != VK_SUCCESS)
        {
            LOGE("vkCreateFramebuffer failed: %s", utils::to_string(result));
            return false;
        }
        debug_name("ce_framebuffer", m_framebuffer);
        return true;
    }
    void init_resources(VkCommandBuffer cmd) const noexcept
    {
        std::vector barriers{
            VkImageMemoryBarrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .image = m_color_image,
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            },
            VkImageMemoryBarrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                    | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .image = m_depth_image,
                .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}
            },
        };
        for (auto image : m_color_swapchain_images)
        {
            barriers.push_back({
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .image = image,
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            });
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
            | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
    }
    [[nodiscard]] std::optional<uint64_t> timeline_value() const noexcept
    {
        uint64_t value = 0;
        if (const VkResult result = vkGetSemaphoreCounterValue(m_device, m_timeline_semaphore, &value);
            result != VK_SUCCESS)
        {
            LOGE("timeline semaphore value failed: %s", utils::to_string(result));
            return std::nullopt;
        }
        return value;
    }
    void present(const std::function<void(const utils::FrameContext& frame)>& update_callback,
        const std::function<void(const utils::FrameContext& frame)>& render_callback) noexcept
    {
        ZoneScoped;

        {
            ZoneScopedN("wait semaphores");
            ZoneValue(m_timeline_values[m_present_index]);
            const VkSemaphoreWaitInfo wait_info{
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                .semaphoreCount = 1,
                .pSemaphores = &m_timeline_semaphore,
                .pValues = &m_timeline_values[m_present_index],
            };
            vkWaitSemaphores(m_device, &wait_info, UINT64_MAX);
        }

        m_timeline_values[m_present_index] = ++m_timeline_counter;
        ZoneValue(m_timeline_values[m_present_index]);

        uint32_t swapchain_index;
        {
            ZoneScopedN("acquire image");
            if (const VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
               m_wait_swapchain[m_present_index], VK_NULL_HANDLE, &swapchain_index); result != VK_SUCCESS)
            {
                LOGE("vkAcquireNextImageKHR failed: %s", utils::to_string(result));
            }
        }

        const auto [width, height] = m_window->size();
        utils::FrameContext frame{
            .cmd = VK_NULL_HANDLE,
            .size = {width, height},
            .color_image = m_color_image,
            .depth_image = m_depth_image,
            .resolve_color_image = m_color_swapchain_images[swapchain_index],
            .color_view = m_color_view,
            .depth_view = m_depth_view,
            .resolve_color_view = m_color_swapchain_views[swapchain_index],
            .framebuffer = m_framebuffer,
            .renderpass = m_renderpass,
            .present_index = m_present_index,
            .timeline_value = m_timeline_counter,
            .display_time = 0,
            .view = {glm::gtx::identity<glm::mat4>()},
            .projection = {glm::gtx::identity<glm::mat4>()},
        };

        update_callback(frame);

        vkResetCommandBuffer(m_present_cmd[m_present_index], 0);
        constexpr VkCommandBufferBeginInfo cmd_begin_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };
        vkBeginCommandBuffer(m_present_cmd[m_present_index], &cmd_begin_info);
        frame.cmd = m_present_cmd[m_present_index];
        render_callback(frame);
        vkEndCommandBuffer(m_present_cmd[m_present_index]);

        const std::array<uint64_t, 2> signal_values = {0, m_timeline_values[m_present_index]};
        const VkTimelineSemaphoreSubmitInfo timeline_submit_info{
            .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
            .signalSemaphoreValueCount = signal_values.size(),
            .pSignalSemaphoreValues = signal_values.data(),
        };
        const std::array signal_semaphores = {m_wait_render[swapchain_index], m_timeline_semaphore };
        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const VkSubmitInfo submit_info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = &timeline_submit_info,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &m_wait_swapchain[m_present_index],
            .pWaitDstStageMask = &wait_stage,
            .commandBufferCount = 1,
            .pCommandBuffers = &m_present_cmd[m_present_index],
            .signalSemaphoreCount = signal_semaphores.size(),
            .pSignalSemaphores = signal_semaphores.data(),
        };
        if (const VkResult result = vkQueueSubmit(m_queue, 1, &submit_info, VK_NULL_HANDLE);
            result != VK_SUCCESS)
        {
            LOGE("Failed to submit");
        }

        {
            ZoneScopedN("queue present");
            const VkPresentInfoKHR present_info {
                .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &m_wait_render[swapchain_index],
                .swapchainCount = 1,
                .pSwapchains = &m_swapchain,
                .pImageIndices = &swapchain_index,
            };
            if (const VkResult result = vkQueuePresentKHR(m_queue, &present_info);
                result != VK_SUCCESS)
            {
                LOGE("vkQueuePresentKHR failed: %s", utils::to_string(result));
            }
        }
        m_present_index = (m_present_index + 1) % swapchain_count();
    }
    void debug_name(const std::string& name, auto obj) const noexcept
    {
        if (vkSetDebugUtilsObjectNameEXT)
        {
            const VkDebugUtilsObjectNameInfoEXT name_info{
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                .objectType = utils::get_type(obj),
                .objectHandle = reinterpret_cast<uint64_t>(obj),
                .pObjectName = name.data(),
            };
            vkSetDebugUtilsObjectNameEXT(m_device, &name_info);
        }
    }
};

struct BufferSuballocation
{
    VmaVirtualAllocation alloc = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    void* ptr = nullptr;
};

class Buffer : NoCopy
{
public:
    Buffer(const Buffer& other) = delete;
    Buffer(Buffer&& other) noexcept = default;
    Buffer& operator=(const Buffer& other) = delete;
    Buffer& operator=(Buffer&& other) noexcept = default;

private:
    std::weak_ptr<Context> m_vk;
    std::string m_name;
    VkDeviceSize m_size = 0;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo m_allocation_info{};
    VkDeviceSize m_staging_size = 0;
    VkBuffer m_staging_buffer = VK_NULL_HANDLE;
    VmaAllocation m_staging_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo m_staging_allocation_info{};
    VmaVirtualBlock m_virtual_block = VK_NULL_HANDLE;

public:
    Buffer() = default;
    Buffer(const std::shared_ptr<Context>& vk, std::string name) noexcept
        : m_vk(vk), m_name(std::move(name)) { }
    ~Buffer() noexcept
    {
        if (m_staging_buffer)
            destroy_staging();
        if (m_buffer)
            destroy();
    }
    [[nodiscard]] VkBuffer buffer() const noexcept { return m_buffer; }
    [[nodiscard]] VkDeviceSize size() const noexcept { return m_size; }
    bool create(const VkDeviceSize size, const VkBufferUsageFlags usage, const VmaMemoryUsage memory_usage,
        const VmaAllocationCreateFlags flags = {0}) noexcept
    {
        const auto vk = m_vk.lock();
        const VkBufferCreateInfo buffer_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = usage,
        };
        const VmaAllocationCreateInfo alloc_info{
            .flags = flags,
            .usage = memory_usage,
        };
        if (const VkResult result = vmaCreateBuffer(vk->vma(), &buffer_info, &alloc_info,
            &m_buffer, &m_allocation, &m_allocation_info); result != VK_SUCCESS)
        {
            LOGE("Failed to create buffer");
            return false;
        }
        vk->debug_name(m_name, m_buffer);
        m_size = size;
        return true;
    }
    [[nodiscard]] std::optional<BufferSuballocation> suballoc(const VkDeviceSize size,
        const VkDeviceSize alignment) noexcept
    {
        if (!m_virtual_block)
        {
            const VmaVirtualBlockCreateInfo block_info{.size = m_size};
            if (const VkResult result = vmaCreateVirtualBlock(&block_info, &m_virtual_block);
                result != VK_SUCCESS)
            {
                LOGE("Failed to create virtual block");
                return std::nullopt;
            }
        }
        const VmaVirtualAllocationCreateInfo alloc_create_info = {.size = size, .alignment = alignment};
        VmaVirtualAllocation alloc;
        VkDeviceSize offset;
        if (const VkResult result = vmaVirtualAllocate(m_virtual_block, &alloc_create_info, &alloc, &offset);
            result != VK_SUCCESS)
        {
            LOGE("Failed to allocate virtual block");
            return std::nullopt;
        }
        return BufferSuballocation{alloc, offset, size, static_cast<uint8_t*>(m_allocation_info.pMappedData) + offset};
    }
    void subfree(const BufferSuballocation& suballoc) noexcept
    {
        if (m_virtual_block)
            vmaVirtualFree(m_virtual_block, suballoc.alloc);
    }
    bool create_staging(const VkDeviceSize staging_size) noexcept
    {
        if (staging_size == 0)
        {
            LOGE("Failed to create staging buffer: staging_size = 0");
            return false;
        }
        const auto vk = m_vk.lock();
        const VkBufferCreateInfo buffer_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = m_size,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        };
        constexpr VmaAllocationCreateInfo alloc_info{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
        };
        if (const VkResult result = vmaCreateBuffer(vk->vma(), &buffer_info, &alloc_info,
            &m_staging_buffer, &m_staging_allocation, &m_staging_allocation_info); result != VK_SUCCESS)
        {
            LOGE("Failed to create staging buffer: %s", utils::to_string(result));
            return false;
        }
        vk->debug_name(std::format("{}_staging", m_name), m_staging_buffer);
        m_staging_size = staging_size;
        return true;
    }
    template<typename T = void*>
    [[nodiscard]] T* staging_ptr() noexcept
    {
        return static_cast<T*>(m_staging_allocation_info.pMappedData);
    }
    void destroy() noexcept
    {
        if (!m_buffer)
        {
            LOGE("Failed to destroy buffer: already destroyed or never created");
            return;
        }
        if (m_virtual_block)
        {
            vmaDestroyVirtualBlock(m_virtual_block);
        }
        if (const auto vk = m_vk.lock())
        {
            vmaDestroyBuffer(vk->vma(), m_buffer, m_allocation);
            m_buffer = VK_NULL_HANDLE;
            m_allocation = VK_NULL_HANDLE;
            m_allocation_info = {};
            m_size = 0;
        }
    }
    void destroy_staging() noexcept
    {
        if (!m_staging_buffer)
        {
            LOGE("Failed to destroy staging buffer: already destroyed");
            return;
        }
        if (const auto vk = m_vk.lock())
        {
            vmaDestroyBuffer(vk->vma(), m_staging_buffer, m_staging_allocation);
            m_staging_buffer = VK_NULL_HANDLE;
            m_staging_allocation = VK_NULL_HANDLE;
            m_staging_allocation_info = {};
            m_staging_size = 0;
        }
    }
    void copy_from(VkCommandBuffer cmd, VkBuffer src_buffer, const BufferSuballocation& src, VkDeviceSize dst_offset) const noexcept
    {
        const VkBufferCopy copy_info{src.offset, dst_offset, src.size};
        vkCmdCopyBuffer(cmd, src_buffer, m_buffer, 1, &copy_info);
    }
    bool update_cmd(VkCommandBuffer cmd, const std::span<const uint8_t> data, VkDeviceSize src_offset, VkDeviceSize offset) const noexcept
    {
        if (!m_staging_buffer)
        {
            LOGE("Failed to update NULL staging buffer");
            return false;
        }
        if (!cmd)
        {
            LOGE("Failed to update staging buffer: NULL command buffer");
            return false;
        }
        if (!m_buffer)
        {
            LOGE("Failed to update NULL buffer");
            return false;
        }
        if (data.size_bytes() > m_staging_size)
        {
            LOGE("Failed to update staging buffer: data bigger than staging size");
            return false;
        }
        if (data.size_bytes() + offset > m_size)
        {
            LOGE("Failed to update buffer: data+offset out of bounds");
            return false;
        }
        std::ranges::copy(data, static_cast<uint8_t*>(m_staging_allocation_info.pMappedData) + src_offset);
        const VkBufferCopy copy_info{src_offset, offset, data.size_bytes()};
        vkCmdCopyBuffer(cmd, m_staging_buffer, m_buffer, 1, &copy_info);
        return true;
    }
    template<typename T>
    bool update_cmd(VkCommandBuffer cmd, const std::span<T> data, VkDeviceSize offset = 0) const noexcept
    {
        return update_cmd(cmd, {reinterpret_cast<const uint8_t*>(data.data()), data.size_bytes()}, 0, offset);
    }
    template<typename T>
    bool update_cmd(VkCommandBuffer cmd, const std::span<const T> data) const noexcept
    {
        return update_cmd(cmd, {reinterpret_cast<const uint8_t*>(data.data()), data.size_bytes()}, 0, 0);
    }
    template<typename T>
    bool update_cmd(VkCommandBuffer cmd, const T& data) const noexcept
    {
        return update_cmd(cmd, std::bit_cast<const std::array<const uint8_t, sizeof(T)>>(data), 0, 0);
    }
};
class ShaderModule
{
protected:
    Context& m_vk;
    std::string m_name;
    VkShaderModule m_module_vs = VK_NULL_HANDLE;
    VkShaderModule m_module_ps = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    ShaderModule(const std::shared_ptr<Context>& vk, std::string name) noexcept
        : m_vk(*vk), m_name(std::move(name)) { }
    [[nodiscard]] std::optional<VkShaderModule> create_shader_module(const std::string& asset_path) const noexcept
    {
        LOGI("Loading shader %s", asset_path.c_str());
        const auto& p = platform::GetPlatform();
        std::vector<uint8_t> shader_code;
        if (auto result = p.read_file(asset_path))
        {
            shader_code = std::move(result.value());
        }
        else
        {
            LOGE("Failed to read shader code: %s", asset_path.c_str());
            return std::nullopt;
        }
        assert(shader_code.size() % sizeof(uint32_t) == 0 && "shader bytecode should be aligned to 4 bytes");
        const VkShaderModuleCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = static_cast<uint32_t>(shader_code.size()),
            .pCode = reinterpret_cast<uint32_t*>(shader_code.data())
        };
        VkShaderModule module{VK_NULL_HANDLE};
        if (const VkResult result = vkCreateShaderModule(m_vk.device(), &info, nullptr, &module);
            result != VK_SUCCESS)
        {
            LOGE("Failed to create shader module: %s", utils::to_string(result));
            return std::nullopt;
        }
        const auto filename = [asset_path]
        {
            if (const auto pos = asset_path.find_last_of('/');
                pos != std::string_view::npos && pos < asset_path.size() - 1)
            {
                return asset_path.substr(pos + 1);
            }
            return asset_path;
        }();
        m_vk.debug_name(filename, module);
        return module;
    }
    bool load_from_file(const std::string& vs_path, const std::string& ps_path) noexcept
    {
        if (auto ps = create_shader_module(ps_path), vs = create_shader_module(vs_path); vs && ps)
        {
            m_module_ps = ps.value();
            m_module_vs = vs.value();
        }
        else
        {
            LOGE("Failed loading shader %s", m_name.c_str());
            return false;
        }
        return true;
    }
public:
    virtual ~ShaderModule() noexcept
    {
        LOGI("Destroying shader module: %s", m_name.c_str());
        if (m_module_vs != VK_NULL_HANDLE)
            vkDestroyShaderModule(m_vk.device(), m_module_vs, nullptr);
        if (m_module_ps != VK_NULL_HANDLE)
            vkDestroyShaderModule(m_vk.device(), m_module_ps, nullptr);
        m_module_vs = VK_NULL_HANDLE;
        m_module_ps = VK_NULL_HANDLE;
    }
};
}
