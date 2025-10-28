module;

#include <print>
#include <memory>
#include <functional>

export module ce.platform.linux;
import ce.platform;

export namespace ce::platform
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
