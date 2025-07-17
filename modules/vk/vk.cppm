module;
#include <string>
#include <utility>
#include <vector>
#include <array>
#include <format>
#include <functional>

#include <volk.h>
#include <vk_mem_alloc.h>

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
import vk.utils;
import ce.platform.globals;

export namespace ce::vk
{
class Context
{
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkRenderPass m_renderpass = VK_NULL_HANDLE;
    VkCommandPool m_cmd_pool_imm = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
    uint32_t m_queue_family_index = std::numeric_limits<uint32_t>::max();
    const uint32_t m_queue_index = 0;
    VmaAllocator m_vma = VK_NULL_HANDLE;
public:
    [[nodiscard]] VkInstance instance() const noexcept { return m_instance; }
    [[nodiscard]] VkDevice device() const noexcept { return m_device; }
    [[nodiscard]] uint32_t queue_index() const noexcept { return m_queue_index; }
    [[nodiscard]] uint32_t queue_family_index() const noexcept { return m_queue_family_index; }
    [[nodiscard]] VmaAllocator vma() const noexcept { return m_vma; }
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
        const VkCommandPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .queueFamilyIndex = m_queue_family_index
        };
        if (const VkResult result = vkCreateCommandPool(m_device, &pool_info, nullptr, &m_cmd_pool_imm); result != VK_SUCCESS)
        {
            LOGE("Failed to create command pool");
            return false;
        }
        debug_name("ce_command_pool_imm", m_cmd_pool_imm);
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
    bool create() noexcept
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
        const std::vector<const char*> layers{
            //"VK_LAYER_KHRONOS_validation"
        };
        const std::vector<const char*> extensions{
            VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef __ANDROID__
            VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
#else
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#endif
        };
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
        debug_name("ce_instance", m_instance);
        volkLoadInstance(m_instance);

        // Find Physical Device
        // TODO: find a suitable device
        debug_name("ce_physical_device", m_physical_device);
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
        if (VkResult result = vkCreateDevice(m_physical_device, &device_info, nullptr, &m_device); result != VK_SUCCESS)
        {
            LOGE("Failed to create Vulkan device: %s", utils::to_string(result));
            return false;
        }
        debug_name("ce_device", m_device);
        volkLoadDevice(m_device);

        const VkDeviceQueueInfo2 get_queue_info{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
            .queueFamilyIndex = m_queue_family_index,
            .queueIndex = m_queue_index
        };
        vkGetDeviceQueue2(m_device, &get_queue_info, &m_queue);
        const VkCommandPoolCreateInfo pool_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .queueFamilyIndex = m_queue_family_index
        };
        if (VkResult result = vkCreateCommandPool(m_device, &pool_info, nullptr, &m_cmd_pool_imm); result != VK_SUCCESS)
        {
            LOGE("Failed to create command pool");
            return false;
        }
        debug_name("ce_command_pool_imm", m_cmd_pool_imm);
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
    bool create_renderpass(const VkFormat color_format, const VkFormat depth_format) noexcept
    {
        const std::array renderpass_attachments{
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
        const std::array renderpass_sub{
            VkSubpassDescription{
                .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .colorAttachmentCount = 1,
                .pColorAttachments = &renderpass_sub_color,
                .pDepthStencilAttachment = &renderpass_sub_depth
            },
        };
        const VkRenderPassCreateInfo renderpass_info{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = static_cast<uint32_t>(renderpass_attachments.size()),
            .pAttachments = renderpass_attachments.data(),
            .subpassCount = static_cast<uint32_t>(renderpass_sub.size()),
            .pSubpasses = renderpass_sub.data()
        };
        if (const VkResult result = vkCreateRenderPass(m_device, &renderpass_info, nullptr, &m_renderpass);
            result != VK_SUCCESS)
        {
            LOGE("Failed to create renderpass");
            return false;
        }
        debug_name("ce_renderpass", m_renderpass);
        return true;
    }
    void debug_name(std::string_view name, auto obj) const noexcept
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
class ShaderModule
{
protected:
    std::weak_ptr<Context> m_vk;
    std::string m_name;
    VkShaderModule m_module_vs{VK_NULL_HANDLE};
    VkShaderModule m_module_ps{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_set_layout{VK_NULL_HANDLE};
    VkPipelineLayout m_layout{VK_NULL_HANDLE};
    VkPipeline m_pipeline{VK_NULL_HANDLE};
    std::vector<VkDescriptorSet> m_descr_sets{};

    [[nodiscard]] std::optional<VkShaderModule> create_shader_module(const std::string& asset_path) const
    {
        LOGI("Loading shader %s", asset_path.c_str());
        const auto& vk = m_vk.lock();
        if (!vk)
        {
            LOGE("Failed to retrieve vulkan Context");
            return std::nullopt;
        }
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
        if (const VkResult result = vkCreateShaderModule(vk->device(), &info, nullptr, &module); result != VK_SUCCESS)
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
        vk->debug_name(filename, module);
        return module;
    }
public:
    ShaderModule(const std::shared_ptr<Context>& vk, std::string name) noexcept
        : m_vk(vk), m_name(std::move(name)) { }
    virtual ~ShaderModule() noexcept
    {
        LOGI("Destroying shader module: %s", m_name.c_str());
        const auto& vk = m_vk.lock();
        if (!vk)
        {
            LOGE("Failed to retrieve vulkan Context");
            return;
        }
        if (m_module_vs != VK_NULL_HANDLE)
            vkDestroyShaderModule(vk->device(), m_module_vs, nullptr);
        if (m_module_ps != VK_NULL_HANDLE)
            vkDestroyShaderModule(vk->device(), m_module_ps, nullptr);
        m_module_vs = VK_NULL_HANDLE;
        m_module_ps = VK_NULL_HANDLE;
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
};
}