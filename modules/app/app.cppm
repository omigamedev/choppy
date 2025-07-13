module;
#include <vector>
#include <memory>
#include <cmath>
#include <volk.h>

export module ce.app;
import ce.platform;
import ce.xr;
import ce.vk;

export namespace ce::app
{
    class AppBase
    {
        std::shared_ptr<platform::Platform> m_platform;
        std::shared_ptr<xr::Context> m_xr;
        std::shared_ptr<vk::Context> m_vk;
    public:
        virtual ~AppBase() = default;
        [[nodiscard]] auto& platform() noexcept { return m_platform; }
        [[nodiscard]] auto& xr() noexcept { return m_xr; }
        [[nodiscard]] auto& vk() noexcept { return m_vk; }
        void init() noexcept
        {
            for (VkImage img : m_xr->swapchain_color_images())
            {
                m_vk->exec_immediate("init", [img](VkCommandBuffer& cmd){
                    const VkImageSubresourceRange subresource_range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 2};
                    const VkImageMemoryBarrier barrier{
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                        .srcAccessMask = 0,
                        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        .image = img,
                        .subresourceRange = subresource_range
                    };
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                });
            }
        }
        void tick(float dt) noexcept
        {
            static float time = 0;
            time += dt;
            m_xr->present([this, t=time](VkImage color){
                m_vk->exec("clear", [color, t](VkCommandBuffer& cmd){
                    const VkImageSubresourceRange subresource_range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 2};

                    const VkImageMemoryBarrier barrier_in{
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        .image = color,
                        .subresourceRange = subresource_range
                    };
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier_in);

                    VkClearColorValue clear_color = {0.0f, 0.0f, fabsf(sinf(t * 0.1f)), 1.0f};
                    vkCmdClearColorImage(cmd, color, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &subresource_range);

                    const VkImageMemoryBarrier barrier_out{
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .image = color,
                        .subresourceRange = subresource_range
                    };
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier_out);
                });
            });
        }
    };

}

