module;

#include <array>
#include <format>
#include <memory>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <glm/gtx/compatibility.hpp>

export module ce.shaders.solidflat;
import ce.shaders;
import ce.vk;
import glm;

export namespace ce::shaders
{
    class SolidFlatShader final : public vk::ShaderModule
    {
    public:
        #include "solid-flat.h"
        [[nodiscard]] static uint32_t constexpr MaxInstance() noexcept { return m_max_instances; };
    private:
        std::shared_ptr<vk::Buffer> m_uniform_frame;
        std::shared_ptr<vk::Buffer> m_uniform_object;
        constexpr static uint32_t m_max_instances = 2048;
        bool create_uniform(const std::shared_ptr<vk::Context>& vk) noexcept
        {
            m_uniform_frame = std::make_shared<vk::Buffer>(vk, "SolidFlatShader::UniformFrame");
            if (!m_uniform_frame->create(sizeof(PerFrameConstants),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT))
            {
                return false;
            }
            m_uniform_object = std::make_shared<vk::Buffer>(vk, "SolidFlatShader::UniformObject");
            if (!m_uniform_object->create(sizeof(PerObjectBuffer) * m_max_instances,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT))
            {
                return false;
            }
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
                // uniforms (type.PerObjectBuffer)
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
        bool create_pipeline(const std::shared_ptr<vk::Context>& vk, VkRenderPass renderpass,
            const VkSampleCountFlagBits sample_count) noexcept
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
                VkVertexInputAttributeDescription{
                    .location = 1,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                    .offset = offsetof(VertexInput, color)
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
            constexpr VkPipelineRasterizationStateCreateInfo rasterization{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .polygonMode = VK_POLYGON_MODE_FILL,
                .cullMode = VK_CULL_MODE_BACK_BIT,
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
            if (const VkResult result = vkCreateGraphicsPipelines(vk->device(), VK_NULL_HANDLE,
                1, &pipeline_info, nullptr, &m_pipeline); result != VK_SUCCESS)
            {
                return false;
            }
            return true;
        }
        bool create_sets(const std::shared_ptr<vk::Context>& vk) noexcept
        {
            // Descriptor Pool
            constexpr std::array descr_pool_sizes{
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, m_max_instances * 2 },
                // VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024 },
                // VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024 },
            };
            const VkDescriptorPoolCreateInfo descr_pool_info{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .maxSets = m_max_instances,
                .poolSizeCount = static_cast<uint32_t>(descr_pool_sizes.size()),
                .pPoolSizes = descr_pool_sizes.data()
            };
            if (const VkResult result = vkCreateDescriptorPool(vk->device(),
                &descr_pool_info, nullptr, &m_descriptor_pool); result != VK_SUCCESS)
            {
                return false;
            }
            vk->debug_name(std::format("{}_descr_pool", m_name), m_descriptor_pool);

            // Descriptor Sets
            m_descr_sets = std::vector(m_max_instances, VkDescriptorSet{VK_NULL_HANDLE});
            const std::vector set_layouts(m_descr_sets.size(), m_set_layout);
            const VkDescriptorSetAllocateInfo set_alloc_info{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = m_descriptor_pool,
                .descriptorSetCount = static_cast<uint32_t>(set_layouts.size()),
                .pSetLayouts = set_layouts.data()
            };
            if (const VkResult result = vkAllocateDescriptorSets(vk->device(),
                &set_alloc_info, m_descr_sets.data()); result != VK_SUCCESS)
            {
                return false;
            }
            std::vector<VkWriteDescriptorSet> writes;
            const VkDescriptorBufferInfo PerFrameConstants_info{
                .buffer = m_uniform_frame->buffer(),
                .offset = 0,
                .range = VK_WHOLE_SIZE,
            };
            std::vector<VkDescriptorBufferInfo> PerObjectBuffer_info(m_max_instances);
            for (uint32_t i = 0; i < m_max_instances; ++i)
            {
                PerObjectBuffer_info[i] = {
                    .buffer = m_uniform_object->buffer(),
                    .offset = sizeof(PerObjectBuffer) * i,
                    .range = sizeof(PerObjectBuffer)
                };
                writes.push_back({
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = m_descr_sets[i],
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pBufferInfo = &PerFrameConstants_info
                });
                writes.push_back({
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = m_descr_sets[i],
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pBufferInfo = &PerObjectBuffer_info[i]
                });
            }
            vkUpdateDescriptorSets(vk->device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            return true;
        }
    public:
        explicit SolidFlatShader(const std::shared_ptr<vk::Context>& vk, const std::string_view name)
            : ShaderModule(vk, std::format("SolidFlat-{}", name)) { }
        ~SolidFlatShader() noexcept override = default;
        [[nodiscard]] VkPipeline pipeline() const noexcept { return m_pipeline; }
        [[nodiscard]] VkPipelineLayout layout() const noexcept { return m_layout; }
        [[nodiscard]] VkDescriptorSet descriptor_set(const uint32_t index) const noexcept { return m_descr_sets[index]; }
        [[nodiscard]] const std::shared_ptr<vk::Buffer>& uniform_frame() const noexcept { return m_uniform_frame; }
        [[nodiscard]] const std::shared_ptr<vk::Buffer>& uniform_object() const noexcept { return m_uniform_object; }
        bool create(VkRenderPass renderpass, const VkSampleCountFlagBits sample_count = VK_SAMPLE_COUNT_1_BIT) noexcept
        {
            const auto vk = m_vk.lock();
            if (!load_from_file(vk, "assets/shaders/solid-flat-vs.spv", "assets/shaders/solid-flat-ps.spv") ||
                !create_uniform(vk) || !create_layout(vk) || !create_pipeline(vk, renderpass, sample_count) || !create_sets(vk))
            {
                return false;
            }
            return true;
        }
    };
}
