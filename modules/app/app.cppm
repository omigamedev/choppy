module;
#include <array>
#include <span>
#include <vector>
#include <memory>
#include <cmath>
#include <volk.h>

#include "vk_mem_alloc.h"
#include "glm/gtx/compatibility.hpp"
// #include "glm/gtx/euler_angles.hpp"
// #include "glm/gtx/quaternion.hpp"

export module ce.app;
import ce.platform;
import ce.platform.globals;
import ce.xr;
import ce.vk;
import ce.shaders.solidflat;
import glm;

using namespace glm;

export namespace ce::app
{
class AppBase final
{
    std::shared_ptr<xr::Context> m_xr;
    std::shared_ptr<vk::Context> m_vk;
    std::shared_ptr<shaders::SolidFlatShader> solid_flat;
    std::shared_ptr<vk::Buffer> m_vertex_buffer;
    std::vector<shaders::SolidFlatShader::VertexInput> vertices;
    shaders::SolidFlatShader::PerFrameConstants uniform{};
public:
    ~AppBase() = default;
    [[nodiscard]] auto& xr() noexcept { return m_xr; }
    [[nodiscard]] auto& vk() noexcept { return m_vk; }
    void init() noexcept
    {
        solid_flat = std::make_shared<shaders::SolidFlatShader>(m_vk, "Test");
        solid_flat->create(m_xr->renderpass());
        vertices = std::vector<shaders::SolidFlatShader::VertexInput>{
            {float3{-0.5f, -0.5f, 0.5f}},
            {float3{-0.5f,  0.5f, 0.5f}},
            {float3{ 0.5f, -0.5f, 0.5f}},
        };
        m_vertex_buffer = std::make_shared<vk::Buffer>(m_vk, "VertexBuffer");
        m_vertex_buffer->create(vertices.size() * sizeof(shaders::SolidFlatShader::VertexInput),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        m_vertex_buffer->create_staging(vertices.size() * sizeof(shaders::SolidFlatShader::VertexInput));
        solid_flat->uniform()->create_staging(sizeof(shaders::SolidFlatShader::PerFrameConstants));
        m_vk->exec_immediate("init resources", [this](VkCommandBuffer cmd){
            m_vertex_buffer->update_cmd<shaders::SolidFlatShader::VertexInput>(cmd, vertices);
            for (VkImage img : m_xr->swapchain_depth_images())
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
        });
        //solid_flat->uniform()->destroy_staging();
        m_vertex_buffer->destroy_staging();
    }
    void tick(const float dt) noexcept
    {
        static float time = 0;
        time += dt;
        m_xr->present([this, t=time](const xr::FrameContext& frame){
            uniform.WorldViewProjection[0] = glm::transpose(frame.projection[0] * frame.view[0]);
            uniform.WorldViewProjection[1] = glm::transpose(frame.projection[1] * frame.view[1]);
            m_vk->exec("render_eye", [this, frame, t](VkCommandBuffer cmd){
                solid_flat->uniform()->update_cmd<shaders::SolidFlatShader::PerFrameConstants>(cmd, uniform);
                const std::array rgb{
                    fabsf(sinf(t * 5.9f)),
                    fabsf(sinf(t * 1.9f)),
                    fabsf(sinf(t * 0.9f))
                };
                const std::array clear_value{
                    VkClearValue{.color = {rgb[0], rgb[1], rgb[2], 1.f}},
                    VkClearValue{.depthStencil = {1.f, 0u}}
                };
                const std::array renderpass_views{frame.color_view, frame.depth_view};
                const VkRenderPassAttachmentBeginInfo renderpass_attachment{
                    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO,
                    .attachmentCount = static_cast<uint32_t>(renderpass_views.size()),
                    .pAttachments = renderpass_views.data()
                };
                const VkRenderPassBeginInfo renderpass_info{
                    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                    .pNext = &renderpass_attachment,
                    .renderPass = frame.renderpass,
                    .framebuffer = frame.framebuffer,
                    .renderArea = VkRect2D{.extent = frame.size},
                    .clearValueCount = static_cast<uint32_t>(clear_value.size()),
                    .pClearValues = clear_value.data(),
                };
                constexpr VkSubpassBeginInfo subpass_begin_info{
                    .sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO,
                    .contents = VK_SUBPASS_CONTENTS_INLINE,
                };
                vkCmdBeginRenderPass2(cmd, &renderpass_info, &subpass_begin_info);

                // Begin rendering

                const VkViewport viewport = m_xr->viewport();
                vkCmdSetViewportWithCount(cmd, 1, &viewport);
                const VkRect2D scissor = m_xr->scissor();
                vkCmdSetScissorWithCount(cmd, 1, &scissor);

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, solid_flat->pipeline());
                const std::array vertex_buffers{m_vertex_buffer->buffer()};
                constexpr std::array vertex_buffers_offset{VkDeviceSize{0}};
                vkCmdBindVertexBuffers(cmd, 0,
                    vertex_buffers.size(), vertex_buffers.data(), vertex_buffers_offset.data());
                const VkDescriptorSet descriptor_set = solid_flat->descriptor_set(0);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    solid_flat->layout(), 0, 1, &descriptor_set, 0, nullptr);
                vkCmdDraw(cmd, 3, 1, 0, 0);

                // End rendering

                constexpr VkSubpassEndInfo subpass_end_info{.sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO};
                vkCmdEndRenderPass2(cmd, &subpass_end_info);
            });
        });
    }
};
}

