module;

#include <array>
#include <format>
#include <memory>
#include <volk.h>
#include <vk_mem_alloc.h>
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/compatibility.hpp"

export module ce.shaders.solidflat;
import ce.shaders;
import ce.vk;
import glm;

using namespace glm;
export namespace ce::shaders
{
    class SolidFlatShader final : public vk::ShaderModule
    {
        #include "solid-flat.h"
        struct UniformsBuffer
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VmaAllocation mem = VK_NULL_HANDLE;
            VmaAllocationInfo mem_info{};
            VkBuffer staging_buffer = VK_NULL_HANDLE;
            VmaAllocation staging_mem = VK_NULL_HANDLE;
            VmaAllocationInfo staging_mem_info{};
            PerFrameConstants* staging_ptr = nullptr; // mapped to staging buffer
        };
        static std::vector<UniformsBuffer> uniforms;
        bool create_uniform() noexcept
        {
            const auto vk = m_vk.lock();
            auto& uniform = uniforms.emplace_back();
            constexpr VkBufferCreateInfo buffer_info{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = sizeof(PerFrameConstants),
                .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            };
            constexpr VmaAllocationCreateInfo buffer_alloc_info{
                .flags = 0,
                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            };
            if (const VkResult result = vmaCreateBuffer(vk->vma(), &buffer_info, &buffer_alloc_info,
                &uniform.buffer, &uniform.mem, &uniform.mem_info); result != VK_SUCCESS)
            {
                return false;
            }
            vk->debug_name(std::format("{}-PerFrameConstants", m_name), uniform.buffer);
            // Create staging buffer
            constexpr VkBufferCreateInfo staging_buffer_info{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = sizeof(PerFrameConstants),
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            };
            constexpr VmaAllocationCreateInfo staging_buffer_alloc_info{
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
            };
            if (const VkResult result = vmaCreateBuffer(vk->vma(), &staging_buffer_info, &staging_buffer_alloc_info,
                &uniform.staging_buffer, &uniform.staging_mem, &uniform.staging_mem_info); result != VK_SUCCESS)
            {
                return false;
            }
            vk->debug_name(std::format("{}-PerFrameConstantsStaging", m_name), uniform.staging_buffer);
            uniform.staging_ptr = reinterpret_cast<PerFrameConstants*>(
                static_cast<uint8_t*>(uniform.staging_mem_info.pMappedData));
            return true;
        }
    public:
        explicit SolidFlatShader(const std::shared_ptr<vk::Context>& vk, const std::string_view name)
            : ShaderModule(vk, std::format("SolidFlat-{}", name)) { }
        ~SolidFlatShader() noexcept override = default;
        bool create() noexcept
        {
            if (!load_from_file("assets/shaders/solid-flat-vs.spv", "assets/shaders/solid-flat-ps.spv"))
            {
                return false;
            }
            if (!create_uniform())
            {
                return false;
            }
            const auto vk = m_vk.lock();
            constexpr std::array set_bindings{
                // uniforms (type.PerFrameConstants)
                VkDescriptorSetLayoutBinding{
                    .binding = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                    .pImmutableSamplers = nullptr
                },
            };
            const VkDescriptorSetLayoutCreateInfo set_info{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .flags = 0,
                .bindingCount = static_cast<uint32_t>(set_bindings.size()),
                .pBindings = set_bindings.data()
            };
            if (const VkResult result = vkCreateDescriptorSetLayout(vk->device(), &set_info, nullptr, &m_set_layout);
                result != VK_SUCCESS)
            {
                return false;
            }
            vk->debug_name(std::format("{}-SetLayout", m_name), m_set_layout);
            // Pipeline layout
            const VkPipelineLayoutCreateInfo layout_info{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .flags = 0,
                .setLayoutCount = 1,
                .pSetLayouts = &m_set_layout,
            };
            if (const VkResult result = vkCreatePipelineLayout(vk->device(), &layout_info, nullptr, &m_layout);
                result != VK_SUCCESS)
            {
                return false;
            }
            vk->debug_name(std::format("{}-PipelineLayout", m_name), m_set_layout);
            return true;
        }
    };
}

std::vector<ce::shaders::SolidFlatShader::UniformsBuffer> ce::shaders::SolidFlatShader::uniforms;
