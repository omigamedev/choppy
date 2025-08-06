module;
#include <optional>
#include <vector>
#include <string_view>
#include <fstream>

export module ce.platform;

export namespace ce::platform
{
enum KeyCodes
{
    Escape = 27,
};
class Window
{
protected:
    uint32_t m_width = 0;
    uint32_t m_height = 0;
public:
    virtual ~Window() = default;
    virtual bool create(uint32_t width, uint32_t height) noexcept = 0;
    [[nodiscard]] virtual std::pair<uint32_t, uint32_t> size() const noexcept { return {m_width, m_height}; };
    [[nodiscard]] virtual uint32_t width() const noexcept { return m_width; }
    [[nodiscard]] virtual uint32_t height() const noexcept { return m_height; }
};
class Platform
{
public:
    virtual ~Platform() = default;

    [[nodiscard]] virtual std::shared_ptr<Window> new_window() const noexcept
    {
        return nullptr;
    }

    [[nodiscard]] virtual std::optional<std::vector<uint8_t>> read_file(const std::string& path) const noexcept
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            return std::nullopt;
        const auto sz = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(sz);
        file.read(reinterpret_cast<char*>(buffer.data()), sz);
        return buffer;
    }
};
}
