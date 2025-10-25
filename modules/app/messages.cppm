module;
#include <cstdint>
#include <span>
#include <optional>
#include <vector>
#include <string>

export module ce.app:messages;
import glm;
import :serializer;

namespace ce::app::messages
{
enum class MessageType : uint16_t
{
    JoinRequest,
    JoinResponse,
    PlayerState,
    PlayerRemoved,
    BlockAction,
};
const char* to_string(const MessageType t)
{
    switch (t)
    {
    case MessageType::PlayerState: return "PlayerState";
    case MessageType::BlockAction: return "BlockAction";
    case MessageType::JoinRequest: return "JoinRequest";
    case MessageType::JoinResponse: return "JoinResponse";
    case MessageType::PlayerRemoved: return "PlayerRemoved";
    default: return "Unknown";
    }
}

struct JoinRequestMessage
{
    MessageType type = MessageType::JoinRequest;
    std::string username{};
    [[nodiscard]] std::vector<uint8_t> serialize() const noexcept
    {
        serializer::MessageWriter w;
        w.write(type);
        w.write(username);
        return std::move(w.buffer);
    }
    [[nodiscard]] static std::optional<JoinRequestMessage> deserialize(
        const std::span<const uint8_t>& message) noexcept
    {
        serializer::MessageReader r(message);
        return JoinRequestMessage {
            .type = r.read<MessageType>(),
            .username = r.read<std::string>()
        };
    }
};

struct JoinResponseMessage
{
    MessageType type = MessageType::JoinResponse;
    bool accepted = false;
    uint32_t new_id = 0;
    [[nodiscard]] std::vector<uint8_t> serialize() const noexcept
    {
        serializer::MessageWriter w;
        w.write(type);
        w.write(accepted);
        w.write(new_id);
        return std::move(w.buffer);
    }
    [[nodiscard]] static std::optional<JoinResponseMessage> deserialize(
        const std::span<const uint8_t>& message) noexcept
    {
        serializer::MessageReader r(message);
        return JoinResponseMessage{
            .type = r.read<MessageType>(),
            .accepted = r.read<bool>(),
            .new_id = r.read<uint32_t>()
        };
    }
};

// struct PlayersStateMessage
// {
//     MessageType type = MessageType::JoinResponse;
//     std::vector<uint32_t> ids;
//     std::vector<std::string> names;
//     [[nodiscard]] std::vector<uint8_t> serialize() const noexcept
//     {
//         serializer::MessageWriter w;
//         w.write(type);
//         w.write(ids);
//         w.write(names);
//         return std::move(w.buffer);
//     }
//     [[nodiscard]] static std::optional<PlayersStateMessage> deserialize(
//         const std::span<const uint8_t>& message) noexcept
//     {
//         serializer::MessageReader r(message);
//         return PlayersStateMessage{
//             .type = r.read<MessageType>(),
//             .ids = r.read<std::vector<uint32_t>>(),
//             .names = r.read<std::vector<std::string>>()
//         };
//     }
// };

struct PlayerStateMessage
{
    MessageType type = MessageType::PlayerState;
    uint32_t id;
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 velocity;
    [[nodiscard]] std::vector<uint8_t> serialize() const noexcept
    {
        serializer::MessageWriter w;
        w.write(type);
        w.write(id);
        w.write(position);
        w.write(rotation);
        w.write(velocity);
        return std::move(w.buffer);
    }
    [[nodiscard]] static std::optional<PlayerStateMessage> deserialize(
        const std::span<const uint8_t>& message) noexcept
    {
        serializer::MessageReader r(message);
        return PlayerStateMessage{
            .type = r.read<MessageType>(),
            .id = r.read<uint32_t>(),
            .position = r.read<glm::vec3>(),
            .rotation = r.read<glm::quat>(),
            .velocity = r.read<glm::vec3>(),
        };
    }
};

struct PlayerRemovedMessage
{
    MessageType type = MessageType::PlayerRemoved;
    uint32_t id;
    [[nodiscard]] std::vector<uint8_t> serialize() const noexcept
    {
        serializer::MessageWriter w;
        w.write(type);
        w.write(id);
        return std::move(w.buffer);
    }
    [[nodiscard]] static std::optional<PlayerRemovedMessage> deserialize(
        const std::span<const uint8_t>& message) noexcept
    {
        serializer::MessageReader r(message);
        return PlayerRemovedMessage{
            .type = r.read<MessageType>(),
            .id = r.read<uint32_t>(),
        };
    }
};

struct BlockActionMessage
{
    MessageType type = MessageType::BlockAction;
    enum class ActionType : uint8_t { Build, Destroy } action;
    glm::ivec3 world_cell;
    [[nodiscard]] std::vector<uint8_t> serialize() const noexcept
    {
        serializer::MessageWriter w;
        w.write(type);
        w.write(action);
        w.write(world_cell);
        return std::move(w.buffer);
    }
    [[nodiscard]] static std::optional<BlockActionMessage> deserialize(
        const std::span<const uint8_t>& message) noexcept
    {
        serializer::MessageReader r(message);
        return BlockActionMessage{
            .type = r.read<MessageType>(),
            .action = r.read<ActionType>(),
            .world_cell = r.read<glm::ivec3>()
        };
    }
};
}
