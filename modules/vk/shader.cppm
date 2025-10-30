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
#else
#define LOGE(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

export module ce.vk.shader;
import ce.vk;
import ce.vk.utils;
import ce.platform.globals;

export namespace ce::vk
{
class ShaderModule
{
protected:
    Context& m_vk;
    std::string m_name;
    VkShaderModule m_module_vs = VK_NULL_HANDLE;
    VkShaderModule m_module_ps = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayout> m_set_layouts;
    std::vector<VkDescriptorPool> m_descriptor_pools;
    std::vector<std::tuple<VkWriteDescriptorSet, VkDescriptorBufferInfo>> writes{};
    std::vector<std::tuple<VkWriteDescriptorSet, VkDescriptorImageInfo>> writes_texture{};

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
    [[nodiscard]] VkPipeline pipeline() const noexcept { return m_pipeline; }
    [[nodiscard]] VkPipelineLayout layout() const noexcept { return m_layout; }
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
    bool reset_descriptors(const uint32_t pool_index) const noexcept
    {
        if (const VkResult result = vkResetDescriptorPool(m_vk.device(), m_descriptor_pools[pool_index], 0);
            result != VK_SUCCESS)
        {
            //TODO: LOGE
            return false;
        }
        return true;
    }
    [[nodiscard]] std::optional<VkDescriptorSet> alloc_descriptor(const uint32_t pool_index,
        const uint32_t set_index) noexcept
    {
        // PerFrame descriptor set
        const VkDescriptorSetAllocateInfo set_alloc_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = m_descriptor_pools[pool_index],
            .descriptorSetCount = 1,
            .pSetLayouts = &m_set_layouts[set_index],
        };
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (const VkResult result = vkAllocateDescriptorSets(m_vk.device(),
            &set_alloc_info, &set); result != VK_SUCCESS)
        {
            return std::nullopt;
        }
        return set;
    }
    void write_buffer(VkDescriptorSet set, const uint32_t binding, VkBuffer buffer,
        const VkDeviceSize offset, const VkDeviceSize range, const VkDescriptorType type) noexcept
    {
        const VkDescriptorBufferInfo buffer_info{
            .buffer = buffer,
            .offset = offset,
            .range = range,
        };
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = binding,
            .descriptorCount = 1,
            .descriptorType = type,
            .pBufferInfo = nullptr, // to be filled in later in update_descriptors()
        };
        writes.emplace_back(write, buffer_info);
    }
    void write_texture(VkDescriptorSet set, const uint32_t binding, VkImageView view, VkSampler sampler) noexcept
    {
        const VkDescriptorImageInfo image_info{
            .sampler = sampler,
            .imageView = view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = binding,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = nullptr, // to be filled in later in update_descriptors()
        };
        writes_texture.emplace_back(write, image_info);
    }
    void update_descriptors() noexcept
    {
        std::vector<VkWriteDescriptorSet> flat_writes{};
        flat_writes.reserve(writes.size() + writes_texture.size());
        for (auto& [write, buffer_info] : writes)
        {
            write.pBufferInfo = &buffer_info;
            flat_writes.emplace_back(write);
        }
        for (auto& [write, image_info] : writes_texture)
        {
            write.pImageInfo = &image_info;
            flat_writes.emplace_back(write);
        }
        vkUpdateDescriptorSets(m_vk.device(),
            static_cast<uint32_t>(flat_writes.size()), flat_writes.data(), 0, nullptr);
        writes.clear();
        writes_texture.clear();
    }
};
}
