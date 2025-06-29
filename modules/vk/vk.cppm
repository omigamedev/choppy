module;
#include <string>
#include <vector>
#include <array>
#include <functional>

#include <volk.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#include <vulkan/vulkan_android.h>
#elifdef _WIN32
#include <stdio.h>
#include <cstdint>
//#include <windows.h>
#define LOGE(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#include <vulkan/vulkan_win32.h>
#endif


export module ce.vk;

export namespace ce::vk
{
class Context
{
    VkInstance m_instance;
    VkDevice m_device;
    VkQueue m_queue;
    VkRenderPass m_renderpass;
    VkCommandPool m_cmd_pool_imm;
    VkPhysicalDevice m_physical_device = nullptr;
    VkPhysicalDeviceProperties2 m_physical_device_properties{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    uint32_t m_queue_family_index = 0;
    uint32_t m_queue_index = 0;
    [[nodiscard]] VkFormat best_color_format(const std::vector<VkFormat>& supported_formats) const noexcept
    {
        for (const auto format : supported_formats)
        {
            VkFormatProperties2 props{ .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
            vkGetPhysicalDeviceFormatProperties2(m_physical_device, format, &props);
            if (props.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
                return format;
        }
        return VK_FORMAT_UNDEFINED;
    }
public:
    [[nodiscard]] VkInstance& instance() noexcept { return m_instance; }
    [[nodiscard]] VkDevice& device() noexcept { return m_device; }
    [[nodiscard]] uint32_t queue_index() const noexcept { return m_queue_index; }
    [[nodiscard]] uint32_t queue_family_index() const noexcept { return m_queue_family_index; }
    bool create_instance(uint32_t api_version, const std::vector<std::string>& external_ext) noexcept
    {
        volkInitialize();
        std::uint32_t vk_version = 0;
        vkEnumerateInstanceVersion(&vk_version);
        LOGI("Found Vulkan runtime %d.%d.%d", VK_API_VERSION_MAJOR(vk_version),
             VK_API_VERSION_MINOR(vk_version), VK_API_VERSION_PATCH(vk_version));
        const VkApplicationInfo app_info{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "VulkanApp",
            .applicationVersion = 0,
            .apiVersion = std::min(api_version, vk_version)
        };
        const std::vector<const char*> layers{
            //"VK_LAYER_KHRONOS_validation"
        };
        std::vector<const char*> extensions{
            VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef __ANDROID__
            VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
#else
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#endif
        };
        for (const std::string& ext : external_ext)
        {
            extensions.emplace_back(ext.c_str());
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
            LOGE("Failed to create Vulkan instance");
            return false;
        }
        volkLoadInstance(m_instance);
        return true;
    }
    bool create_device(VkPhysicalDevice physical_device, const std::vector<std::string>& external_ext) noexcept
    {
        m_physical_device = physical_device;
        vkGetPhysicalDeviceProperties2(physical_device, &m_physical_device_properties);
        LOGI("Vulkan device: %s", m_physical_device_properties.properties.deviceName);

        const std::vector<VkQueueFamilyProperties2> queue_props = [&]{
            uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties2(physical_device, &count, nullptr);
            std::vector<VkQueueFamilyProperties2> props(count, {.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2});
            vkGetPhysicalDeviceQueueFamilyProperties2(physical_device, &count, props.data());
            return props;
        }();

        for (int i = 0; i < queue_props.size(); i++)
        {
            const auto queue_flags = queue_props[i].queueFamilyProperties.queueFlags;
            const auto has_graphics = queue_flags & VK_QUEUE_GRAPHICS_BIT;
            const auto has_compute = queue_flags & VK_QUEUE_COMPUTE_BIT;
            if (has_graphics && has_compute)
            {
                m_queue_family_index = i;
                break;
            }
        }

        const std::vector<const char*> layers{};
        std::vector<const char*> extensions{};
        for (auto& ext : external_ext)
        {
            extensions.emplace_back(ext.c_str());
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
            .enabledLayerCount = static_cast<uint32_t>(layers.size()),
            .ppEnabledLayerNames = layers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data()
        };
        if (VkResult result = vkCreateDevice(physical_device, &device_info, nullptr, &m_device); result != VK_SUCCESS)
        {
            LOGE("Failed to create Vulkan device");
            return false;
        }
        volkLoadDevice(m_device);

        const VkDeviceQueueInfo2 get_queue_info{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
            .queueFamilyIndex = m_queue_family_index,
            .queueIndex = m_queue_index
        };
        vkGetDeviceQueue2(m_device, &get_queue_info, &m_queue);
        const VkCommandPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .queueFamilyIndex = m_queue_family_index
        };
        vkCreateCommandPool(m_device, &pool_info, nullptr, &m_cmd_pool_imm);

        return true;
    }
    void exec_immediate(std::function<void(VkCommandBuffer& cmd)> fn) const noexcept
    {
        VkFence fence = VK_NULL_HANDLE;
        constexpr VkFenceCreateInfo fence_info{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        };
        if (VkResult result = vkCreateFence(m_device, &fence_info, nullptr, &fence); result != VK_SUCCESS)
        {
            LOGE("Failed to create fence");
            return;
        }
        const VkCommandBufferAllocateInfo cmd_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = m_cmd_pool_imm,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (VkResult result = vkAllocateCommandBuffers(m_device, &cmd_info, &cmd); result != VK_SUCCESS)
        {
            LOGE("Failed to allocate command buffer");
            vkDestroyFence(m_device, fence, nullptr);
            return;
        }
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
        if (VkResult result = vkWaitForFences(m_device, 1, &fence, true, std::numeric_limits<uint64_t>::max()); result != VK_SUCCESS)
        {
            LOGE("Failed to wait for fence");
        }
        vkFreeCommandBuffers(m_device, m_cmd_pool_imm, 1, &cmd);
        vkDestroyFence(m_device, fence, nullptr);
    }
    bool create_renderpass(const std::vector<VkFormat>& supported_formats)
    {
        const VkFormat color_format = best_color_format(supported_formats);
        const VkFormat depth_format = VK_FORMAT_D16_UNORM;
        const std::array<const VkAttachmentDescription, 2> renderpass_attachments{
            VkAttachmentDescription{
                .format = color_format,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            },
            VkAttachmentDescription{
                .format = depth_format,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                .finalLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
            },
        };
        constexpr VkAttachmentReference renderpass_sub_color{
                .attachment = 0,
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };
        constexpr VkAttachmentReference renderpass_sub_depth{
                .attachment = 1,
                .layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
        };
        const std::array<const VkSubpassDescription, 1> renderpass_sub{
            VkSubpassDescription{
                .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .colorAttachmentCount = 1,
                .pColorAttachments = &renderpass_sub_color,
                .pDepthStencilAttachment = &renderpass_sub_depth
            },
        };
        const VkRenderPassCreateInfo renderpass_info{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = (uint32_t)renderpass_attachments.size(),
            .pAttachments = renderpass_attachments.data(),
            .subpassCount = (uint32_t)renderpass_sub.size(),
            .pSubpasses = renderpass_sub.data()
        };
        if (VkResult result = vkCreateRenderPass(m_device, &renderpass_info, nullptr, &m_renderpass); result != VK_SUCCESS)
        {
            LOGE("Failed to create renderpass");
            return false;
        }
        return true;
    }
};
}