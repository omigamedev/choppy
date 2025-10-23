module;

#include <array>
#include <vector>
#include <format>
#include <memory>
#include <optional>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <glm/gtx/compatibility.hpp>

export module ce.shaders.solidcolor;
import ce.vk;
import ce.vk.shader;
import glm;

export namespace ce::shaders
{
class SolidColorShader final : public vk::ShaderModule
{
public:
    #include "solid-color.h"
private:
    bool create_layout()
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
            // uniforms (type.PerObjectBuffer)
            VkDescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .pImmutableSamplers = nullptr
            },
        };
        const std::array set_info{
            // Per frame set
            VkDescriptorSetLayoutCreateInfo{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .flags = 0,
                .bindingCount = 1,
                .pBindings = set_bindings.data()
            },
            // Per Object set
            VkDescriptorSetLayoutCreateInfo{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .flags = 0,
                .bindingCount = 1,
                .pBindings = set_bindings.data() + 1
            },
        };
        m_set_layouts.resize(2);
        if (const VkResult result = vkCreateDescriptorSetLayout(m_vk.device(), &set_info[0],
            nullptr, &m_set_layouts[0]);  result != VK_SUCCESS)
        {
            return false;
        }
        m_vk.debug_name(std::format("{}-FrameSetLayout", m_name), m_set_layouts[0]);
        if (const VkResult result = vkCreateDescriptorSetLayout(m_vk.device(), &set_info[1],
            nullptr, &m_set_layouts[1]); result != VK_SUCCESS)
        {
            return false;
        }
        m_vk.debug_name(std::format("{}-FrameSetLayout", m_name), m_set_layouts[1]);
        // Pipeline layout
        const VkPipelineLayoutCreateInfo layout_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .flags = 0,
            .setLayoutCount = static_cast<uint32_t>(m_set_layouts.size()),
            .pSetLayouts = m_set_layouts.data(),
        };
        if (const VkResult result = vkCreatePipelineLayout(m_vk.device(), &layout_info, nullptr, &m_layout);
            result != VK_SUCCESS)
        {
            return false;
        }
        m_vk.debug_name(std::format("{}-PipelineLayout", m_name), m_layout);
        return true;
    }
    bool create_pipeline(VkRenderPass renderpass, const VkSampleCountFlagBits sample_count,
        const bool enable_blending, const bool double_sided, const bool wireframe) noexcept
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
                .stride = sizeof(VertexInput),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
            }
        };
        const std::array input_attribute{
            VkVertexInputAttributeDescription{
                .location = 0,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                .offset = offsetof(VertexInput, position)
            },
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
        const VkCullModeFlags cull_mode = double_sided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
        const VkPolygonMode poly_mode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        const VkPipelineRasterizationStateCreateInfo rasterization{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = poly_mode,
            .cullMode = cull_mode,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
            .lineWidth = 1,
        };
        const VkPipelineMultisampleStateCreateInfo multisample{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = sample_count,
            .sampleShadingEnable = false
        };
        constexpr VkPipelineDepthStencilStateCreateInfo depth{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = false,
            .depthWriteEnable = false,
            .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
            .stencilTestEnable = false,
        };
        const VkPipelineColorBlendAttachmentState blend_color{
            .blendEnable = enable_blending,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .alphaBlendOp = VK_BLEND_OP_ADD,
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
            VK_DYNAMIC_STATE_LINE_WIDTH,
            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
            // VK_DYNAMIC_STATE_POLYGON_MODE_EXT,
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
        if (const VkResult result = vkCreateGraphicsPipelines(m_vk.device(), VK_NULL_HANDLE,
            1, &pipeline_info, nullptr, &m_pipeline); result != VK_SUCCESS)
        {
            return false;
        }
        return true;
    }
    bool create_pools(const uint32_t pools_count, const uint32_t frame_sets, const uint32_t object_sets) noexcept
    {
        m_descriptor_pools.resize(pools_count);
        for (size_t i = 0; i < pools_count; i++)
        {
            const std::array descr_pool_sizes{
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame_sets },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, object_sets },
            };
            const VkDescriptorPoolCreateInfo descr_pool_info{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .maxSets = frame_sets + object_sets,
                .poolSizeCount = static_cast<uint32_t>(descr_pool_sizes.size()),
                .pPoolSizes = descr_pool_sizes.data()
            };
            if (const VkResult result = vkCreateDescriptorPool(m_vk.device(),
                &descr_pool_info, nullptr, &m_descriptor_pools[i]); result != VK_SUCCESS)
            {
                return false;
            }
            m_vk.debug_name(std::format("{}_descr_pool[{}]", m_name, i), m_descriptor_pools[i]);
        }
        return true;
    }
public:
    explicit SolidColorShader(const std::shared_ptr<vk::Context>& vk, const std::string_view name)
        : ShaderModule(vk, std::format("SolidColor-{}", name)) { }
    ~SolidColorShader() noexcept override = default;
    bool create(VkRenderPass renderpass, const uint32_t pools_count, const VkSampleCountFlagBits sample_count,
        const uint32_t frame_sets, const uint32_t object_sets, const bool enable_blending, const bool double_sided, const bool wireframe) noexcept
    {
        if (!load_from_file("assets/shaders/solid-color-vs.spv", "assets/shaders/solid-color-ps.spv") ||
            !create_layout() || !create_pipeline(renderpass, sample_count, enable_blending, double_sided, wireframe) ||
            !create_pools(pools_count, frame_sets, object_sets))
        {
            return false;
        }
        return true;
    }
};
}
