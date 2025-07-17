module;
#include <optional>
#include <vector>
#include <string_view>
#include <fstream>

export module ce.platform;

export namespace ce::platform
{
class Window
{
public:
};
class Platform
{
public:
    virtual ~Platform() = default;

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
