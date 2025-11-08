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
    WorldData,
    ChunkData,
    RTCJson,
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
    case MessageType::WorldData: return "WorldData";
    default: return "Unknown";
    }
}
enum class MessageDirection : uint8_t
{
    Request,
    Response,
};
const char* to_string(const MessageDirection t)
{
    switch (t)
    {
    case MessageDirection::Request: return "Request";
    case MessageDirection::Response: return "Response";
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

struct ChunkDataMessage
{
    MessageType type = MessageType::ChunkData;
    MessageDirection message_direction;
    glm::ivec3 sector;
    std::vector<uint8_t> data;
    [[nodiscard]] std::vector<uint8_t> serialize() const noexcept
    {
        serializer::MessageWriter w;
        w.write(type);
        w.write(message_direction);
        w.write(sector);
        w.write(data);
        return std::move(w.buffer);
    }
    [[nodiscard]] static std::optional<ChunkDataMessage> deserialize(
        const std::span<const uint8_t>& message) noexcept
    {
        serializer::MessageReader r(message);
        return ChunkDataMessage{
            .type = r.read<MessageType>(),
            .message_direction = r.read<MessageDirection>(),
            .sector = r.read<glm::ivec3>(),
            .data = r.read<std::vector<uint8_t>>(),
        };
    }
};

struct WorldDataMessage
{
    MessageType type = MessageType::WorldData;
    std::vector<uint8_t> data;
    [[nodiscard]] std::vector<uint8_t> serialize() const noexcept
    {
        serializer::MessageWriter w;
        w.write(type);
        w.write(data);
        return std::move(w.buffer);
    }
    [[nodiscard]] static std::optional<WorldDataMessage> deserialize(
        const std::span<const uint8_t>& message) noexcept
    {
        serializer::MessageReader r(message);
        return WorldDataMessage{
            .type = r.read<MessageType>(),
            .data = r.read<std::vector<uint8_t>>(),
        };
    }
};

struct BlockActionMessage
{
    MessageType type = MessageType::BlockAction;
    enum class ActionType : uint8_t { Build, Break } action;
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

struct RTCJsonMessage
{
    MessageType type = MessageType::RTCJson;
    uint32_t id;
    std::string json_string;
    [[nodiscard]] std::vector<uint8_t> serialize() const noexcept
    {
        serializer::MessageWriter w;
        w.write(type);
        w.write(id);
        w.write(json_string);
        return std::move(w.buffer);
    }
    [[nodiscard]] static std::optional<RTCJsonMessage> deserialize(
        const std::span<const uint8_t>& message) noexcept
    {
        serializer::MessageReader r(message);
        return RTCJsonMessage{
            .type = r.read<MessageType>(),
            .id = r.read<uint32_t>(),
            .json_string = r.read<std::string>(),
        };
    }
};
}
