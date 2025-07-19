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
        bool create_uniform(const std::shared_ptr<vk::Context>& vk) noexcept
        {
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
        bool create_layout(const std::shared_ptr<vk::Context>& vk)
        {
            constexpr std::array set_bindings{
                // uniforms (type.PerFrameConstants)
                VkDescriptorSetLayoutBinding{
                    .binding = 0,
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
        bool create_pipeline(const std::shared_ptr<vk::Context>& vk, VkRenderPass renderpass) noexcept
        {
            // Pipeline
            const std::array stages{
                VkPipelineShaderStageCreateInfo{
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_VERTEX_BIT,
                    .module = m_module_vs,
                    .pName = "VSMain",
                    .pSpecializationInfo = nullptr
                },
                VkPipelineShaderStageCreateInfo{
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .module = m_module_ps,
                    .pName = "PSMain",
                    .pSpecializationInfo = nullptr
                },
            };
            constexpr std::array input_binding{
                VkVertexInputBindingDescription{
                    .binding = 0,
                    .stride = sizeof(VertexInput::position),
                    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
                }
            };
            const std::array input_attribute{
                VkVertexInputAttributeDescription{
                    .location = 0,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = offsetof(VertexInput, position)
                }
            };
            const VkPipelineVertexInputStateCreateInfo input{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                .vertexBindingDescriptionCount = static_cast<uint32_t>(input_binding.size()),
                .pVertexBindingDescriptions = input_binding.data(),
                .vertexAttributeDescriptionCount = static_cast<uint32_t>(input_attribute.size()),
                .pVertexAttributeDescriptions = input_attribute.data()
            };
            constexpr VkPipelineInputAssemblyStateCreateInfo assembly{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
            };
            constexpr VkPipelineViewportStateCreateInfo viewport{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            };
            constexpr VkPipelineRasterizationStateCreateInfo rasterization{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .polygonMode = VK_POLYGON_MODE_FILL,
                .cullMode = VK_CULL_MODE_NONE,
                .frontFace = VK_FRONT_FACE_CLOCKWISE,
                .lineWidth = 1,
            };
            constexpr VkPipelineMultisampleStateCreateInfo multisample{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
                .sampleShadingEnable = false
            };
            constexpr VkPipelineDepthStencilStateCreateInfo depth{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .depthTestEnable = true,
                .depthWriteEnable = true,
                .depthCompareOp = VK_COMPARE_OP_LESS,
                .stencilTestEnable = false,
            };
            constexpr VkPipelineColorBlendAttachmentState blend_color{
                .blendEnable = false,
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
            };
            const VkPipelineColorBlendStateCreateInfo blend{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .attachmentCount = 1,
                .pAttachments = &blend_color
            };
            constexpr std::array dynamic_states{
                VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
                VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
            };
            const VkPipelineDynamicStateCreateInfo dynamics{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
                .pDynamicStates = dynamic_states.data()
            };
            const VkGraphicsPipelineCreateInfo pipeline_info{
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .stageCount = static_cast<uint32_t>(stages.size()),
                .pStages = stages.data(),
                .pVertexInputState = &input,
                .pInputAssemblyState = &assembly,
                .pTessellationState = nullptr,
                .pViewportState = &viewport,
                .pRasterizationState = &rasterization,
                .pMultisampleState = &multisample,
                .pDepthStencilState = &depth,
                .pColorBlendState = &blend,
                .pDynamicState = &dynamics,
                .layout = m_layout,
                .renderPass = renderpass,
                .subpass = 0
            };
            if (const VkResult result = vkCreateGraphicsPipelines(vk->device(), VK_NULL_HANDLE,
                1, &pipeline_info, nullptr, &m_pipeline); result != VK_SUCCESS)
            {
                return false;
            }
            return true;
        }
    public:
        explicit SolidFlatShader(const std::shared_ptr<vk::Context>& vk, const std::string_view name)
            : ShaderModule(vk, std::format("SolidFlat-{}", name)) { }
        ~SolidFlatShader() noexcept override = default;
        bool create(const VkRenderPass renderpass) noexcept
        {
            const auto vk = m_vk.lock();
            if (!load_from_file(vk, "assets/shaders/solid-flat-vs.spv", "assets/shaders/solid-flat-ps.spv") ||
                !create_uniform(vk) || !create_layout(vk) || !create_pipeline(vk, renderpass))
            {
                return false;
            }
            return true;
        }
    };
}

std::vector<ce::shaders::SolidFlatShader::UniformsBuffer> ce::shaders::SolidFlatShader::uniforms;
