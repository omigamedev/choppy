module;
#include <cstdint>
#include <span>
#include <optional>
#include <vector>

export module ce.app:messages;
import glm;

namespace ce::app::messages
{
enum class MessageType : uint16_t
{
    UpdatePos,
};
struct MessageReader
{
    off_t offset = 0;
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
};
template<typename Message>
[[nodiscard]] std::vector<uint8_t> serialize(const Message& message) noexcept
{
    static_assert(false, "Not Implemented");
}
template<typename Message>
[[nodiscard]] std::optional<Message> deserialize(const std::span<const uint8_t>& message) noexcept
{
    static_assert(false, "Not Implemented");
}

struct UpdatePosMessage
{
    MessageType type = MessageType::UpdatePos;
    glm::vec3 position;
    glm::quat rotation;
};
[[nodiscard]] std::vector<uint8_t> serialize(const UpdatePosMessage& message) noexcept
{
    MessageWriter w;
    w.write(message.type);
    w.write(message.position);
    w.write(message.rotation);
    return std::move(w.buffer);
}
template<>
[[nodiscard]] std::optional<UpdatePosMessage> deserialize<UpdatePosMessage>(const std::span<const uint8_t>& message) noexcept
{
    MessageReader r(message);
    const UpdatePosMessage m{
        .type = r.read<MessageType>(),
        .position = r.read<glm::vec3>(),
        .rotation = r.read<glm::quat>()
    };
    return m;
}
}
