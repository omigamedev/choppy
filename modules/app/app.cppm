module;
#include <array>
#include <vector>
#include <memory>
#include <cmath>
#include <volk.h>

export module ce.app;
import ce.platform;
import ce.platform.globals;
import ce.xr;
import ce.vk;
import ce.shaders.solidflat;
import glm;

export namespace ce::app
{
class AppBase final
{
    std::shared_ptr<xr::Context> m_xr;
    std::shared_ptr<vk::Context> m_vk;
    std::shared_ptr<shaders::SolidFlatShader> solid_flat;
public:
    virtual ~AppBase() = default;
    [[nodiscard]] auto& xr() noexcept { return m_xr; }
    [[nodiscard]] auto& vk() noexcept { return m_vk; }
    void init() noexcept
    {
        m_vk->exec_immediate("depth_init_layout", [this](VkCommandBuffer cmd){
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
        solid_flat = std::make_shared<shaders::SolidFlatShader>(m_vk, "Test");
        solid_flat->create(m_xr->renderpass());
    }
    void tick(const float dt) const noexcept
    {
        static float time = 0;
        time += dt;
        m_xr->present([this, t=time](const xr::FrameContext& frame){
            m_vk->exec("clear", [frame, t](VkCommandBuffer cmd){
                constexpr VkImageSubresourceRange subresource_range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 2};
                // const VkImageMemoryBarrier barrier_in{
                //     .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                //     .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                //     .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                //     .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                //     .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                //     .image = color,
                //     .subresourceRange = subresource_range
                // };
                // vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                //     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier_in);
                //
                // const VkClearColorValue clear_color = {fabsf(sinf(t * 5.9f)), fabsf(sinf(t * 1.9f)), fabsf(sinf(t * 10.9f)), 1.0f};
                // vkCmdClearColorImage(cmd, color, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &subresource_range);
                //
                // const VkImageMemoryBarrier barrier_out{
                //     .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                //     .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                //     .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                //     .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                //     .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                //     .image = color,
                //     .subresourceRange = subresource_range
                // };
                // vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                //     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier_out);
                const std::array rgb{
                    fabsf(sinf(t * 5.9f)),
                    fabsf(sinf(t * 1.9f)),
                    fabsf(sinf(t * 10.9f))
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

                // End rendering

                constexpr VkSubpassEndInfo subpass_end_info{.sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO};
                vkCmdEndRenderPass2(cmd, &subpass_end_info);
            });
        });
    }
};
}

