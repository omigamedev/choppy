module;
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
        std::shared_ptr<ce::platform::Platform> m_platform;
        std::shared_ptr<ce::xr::Context> m_xr;
        std::shared_ptr<ce::vk::Context> m_vk;
    public:
        virtual ~AppBase() = default;
        [[nodiscard]] auto& platform() noexcept { return m_platform; }
        [[nodiscard]] auto& xr() noexcept { return m_xr; }
        [[nodiscard]] auto& vk() noexcept { return m_vk; }
        void tick(float dt) noexcept
        {
            static float time = 0;
            time += dt;
            m_xr->present([this, t=time](VkImage color){
                m_vk->exec([color, t](VkCommandBuffer& cmd){
                    VkClearColorValue clear_color = {0.0f, 0.0f, fabsf(sinf(t * 0.1f)), 1.0f};
                    VkImageSubresourceRange subresource_range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 2};
                    vkCmdClearColorImage(cmd, color, VK_IMAGE_LAYOUT_GENERAL, &clear_color, 1, &subresource_range);
                });
            });

        }
        bool on_init() noexcept
        {
            return true;
        }
        bool create_surface() noexcept
        {
            return true;
        }
    };

}

