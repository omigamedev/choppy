module;
export module ce.platform;
import ce.xr;
import ce.vk;

export namespace ce::app
{
    class AppBase
    {
        ce::xr::Instance xr_instance;
        ce::vk::Context vk_context;
    public:
        virtual ~AppBase() = default;
        bool init() noexcept
        {
            return true;
        }
        bool create_surface() noexcept
        {
            return true;
        }
    };

}

