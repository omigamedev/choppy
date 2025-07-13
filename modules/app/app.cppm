module;
#include <memory>
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
        void tick(float delta_seconds) noexcept
        {

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

