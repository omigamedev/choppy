module;
#include <cstdint>
#include <memory>

export module ce.platform.globals;
import ce.platform;

#ifdef WIN32
import ce.platform.win32;
ce::platform::Win32 platform_storage{};
#elifdef __ANDROID__
import ce.platform.android;
ce::platform::Android platform_storage{};
#else
//import ce.platform.linux;
namespace ce::platform
{
class LinuxWindow final : public Window
{
public:
    bool create(const uint32_t width, const uint32_t height) noexcept override
    {
        return true;
    }
};
class Linux final : public Platform
{
public:
    [[nodiscard]] std::shared_ptr<Window> new_window() const noexcept override
    {
        return std::make_shared<LinuxWindow>();
    }
};    
}
ce::platform::Linux platform_storage{};
#endif

export namespace ce::platform
{
    template<typename T = Platform>
    T& GetPlatform() noexcept
    {
        return static_cast<T&>(platform_storage);
    }
}
