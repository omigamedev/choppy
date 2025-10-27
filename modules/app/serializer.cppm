module;
#include <span>
#include <string>
#include <vector>
export module ce.app:serializer;

export namespace ce::app::serializer
{

struct MessageReader
{
    size_t offset = 0;
    const std::span<const uint8_t> message;
    MessageReader(const std::span<const uint8_t>& message) noexcept : message(message) {}
    template<typename T> [[nodiscard]] T read() noexcept
    {
        T value = *reinterpret_cast<const T*>(message.data() + offset);
        offset += sizeof(T);
        return value;
    }
    template<typename T> void read(T& out) noexcept
    {
        out = *reinterpret_cast<const T*>(message.data() + offset);
        offset += sizeof(T);
    }
    template<> std::string read<std::string>() noexcept
    {
        std::string value;
        read(value);
        return value;
    }
    template<> void read(std::string& out) noexcept
    {
        const uint16_t size = read<uint16_t>();
        out = std::string(reinterpret_cast<const char*>(message.data() + sizeof(size)), size);
        offset += out.size();
    }
};
struct MessageWriter
{
    std::vector<uint8_t> buffer;
    MessageWriter() = default;
    template<typename Message> MessageWriter() noexcept
    {
        buffer.reserve(sizeof(Message));
    }
    template<typename T> void write(const T& value) noexcept
    {
        buffer.append_range(std::span(reinterpret_cast<const uint8_t*>(&value), sizeof(T)));
    }
    template<> void write(const std::string& value) noexcept
    {
        write<uint16_t>(value.size());
        buffer.append_range(std::span(reinterpret_cast<const uint8_t*>(value.c_str()), value.size()));
    }
};

}
