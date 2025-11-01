module;
#include <ranges>
#include <string>
#include <cstdio>
#include <enet.h>
#include <format>
#include <unordered_map>
#include <tracy/Tracy.hpp>
#include <rtc/rtc.hpp>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#else
#define LOGE(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

export module ce.app:server;
import :utils;
import :player;
import :resources;
import :physics;
import :messages;
import ce.shaders.solidcolor;
import ce.vk.utils;

export namespace ce::app::server
{
class ServerSystem : utils::NoCopy
{
    static constexpr uint32_t MaxClients = 32;
    std::shared_ptr<rtc::WebSocketServer> wss;
    std::vector<std::shared_ptr<rtc::WebSocket>> ws_clients;
    ENetHost* server = nullptr;
    uint32_t client_ids = 1;
    std::unordered_map<ENetPeer*, player::PlayerState> clients;
    std::vector<player::PlayerState> removed_players;
    [[nodiscard]] std::string address2str(const ENetAddress& address) const noexcept
    {
        char ipStr[INET6_ADDRSTRLEN] = {0};
        // ENetAddress can contain either IPv4 or IPv6.
        const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(&address.host);
        inet_ntop(AF_INET6, &addr6->sin6_addr, ipStr, sizeof(ipStr));
        return std::format("{}:{}", ipStr, address.port);
    }
public:
    uint32_t player_id = 1;
    glm::vec3 player_pos = glm::vec3(0, 0, 0);
    glm::quat player_rot = glm::gtc::identity<glm::quat>();
    glm::vec3 player_vel = glm::vec3(0, 0, 0);
    std::function<void(const messages::BlockActionMessage&)> on_block_action;
    std::function<void(ENetPeer* peer, const messages::ChunkDataMessage&)> on_chunk_data_request;
    bool create_system() noexcept
    {
        if (enet_initialize() != 0)
        {
            LOGE("An error occurred while initializing ENet.");
            return false;
        }
        const ENetAddress address = {
            .host = ENET_HOST_ANY, // Bind the server to the default localhost.
            .port = 7777, // Bind the server to port 7777.
        };
        server = enet_host_create(&address, MaxClients, 2, 0, 0);
        if (!server)
        {
            LOGE("An error occurred while trying to create an ENet server host.");
            return false;
        }
        wss = std::make_shared<rtc::WebSocketServer>(rtc::WebSocketServerConfiguration{
            .port = 7778,
            .enableTls = false,
            // .bindAddress = ,
            // .connectionTimeout = ,
            // .maxMessageSize =
        });
        wss->onClient([this](const std::shared_ptr<rtc::WebSocket>& socket)
        {
            LOGI("web-socket new client");
            ws_clients.push_back(socket);
            socket->onMessage([this, socket](const rtc::message_variant& data)
            {
                LOGI("web-socket on message");
                if (std::holds_alternative<rtc::binary>(data))
                {
                    const auto& bin = std::get<rtc::binary>(data);
                    ws_parse_message(socket, {reinterpret_cast<const uint8_t*>(bin.data()), bin.size()});
                }
                else
                {
                    LOGE("web-socket on message: string data %s",
                        std::get<std::string>(data).c_str());
                }
            });
            socket->onClosed([this, socket]
            {
                LOGI("web-socket on close");
                std::erase(ws_clients, socket);
            });
            socket->onError([this, socket](const std::string& error)
            {
                LOGI("web-socket on error: %s", error.c_str());
                std::erase(ws_clients, socket);
            });
        });
        return true;
    }
    void destroy_system() noexcept
    {
        for (auto& player : std::views::values(clients))
        {
            globals::m_resources->destroy_geometry(player.cube, 0);
            player.destroy();
        }
        if (server)
        {
            enet_host_destroy(server);
            server = nullptr;
        }
        enet_deinitialize();
    }
    template<typename T>
    void send_message(ENetPeer* peer, const uint32_t enet_flags, const T& message) const noexcept
    {
        const auto buffer = message.serialize();
        ENetPacket* packet = enet_packet_create(buffer.data(), buffer.size(), enet_flags);
        enet_peer_send(peer, 0, packet);
        //LOGI("sent message of type: %s", messages::to_string(message.type));
    }
    template<typename T>
    void broadcast_message(const uint32_t enet_flags, const T& message) const noexcept
    {
        const auto buffer = message.serialize();
        ENetPacket* packet = enet_packet_create(buffer.data(), buffer.size(), enet_flags);
        for (ENetPeer* peer : std::views::keys(clients))
            enet_peer_send(peer, 0, packet);
        //LOGI("sent message of type: %s", messages::to_string(message.type));
    }
    template<typename T>
    void ws_send_message(const std::shared_ptr<rtc::WebSocket>& socket, const T& message) const noexcept
    {
        const auto buffer = message.serialize();
        if (socket && socket->isOpen())
        {
            socket->send(reinterpret_cast<const std::byte*>(buffer.data()), buffer.size());
        }
    }
    void add_player(ENetPeer* peer) noexcept
    {
        player::PlayerState player{
            .cube = globals::headless ?
                resources::Geometry{} : globals::m_resources->create_cube<shaders::SolidColorShader>()
        };
        clients.emplace(std::pair(peer, player));
    }
    void remove_player(ENetPeer* peer) noexcept
    {
        const uint32_t id = clients[peer].id;
        removed_players.emplace_back(clients[peer]);  // TODO: in headless mode just remove it, no need to garbage collect
        clients.erase(peer);
        for (const auto& [send_peer, player] : clients)
        {
            if (peer != send_peer)
            {
                send_message(send_peer, ENET_PACKET_FLAG_RELIABLE,
                    messages::PlayerRemovedMessage{.id = id});
            }
        }
    }
    [[nodiscard]] auto get_players() noexcept
    {
        return std::views::values(clients);
    }
    void ws_parse_message(const std::shared_ptr<rtc::WebSocket>& socket, std::span<const uint8_t> message) noexcept
    {
        const messages::MessageType type = *reinterpret_cast<const messages::MessageType*>(message.data());
        switch (type)
        {
        case messages::MessageType::JoinResponse:
            LOGI("WS: MessageType::JoinResponse");
            if (const auto response = messages::JoinResponseMessage::deserialize(message))
            {
                ws_send_message(socket, messages::WorldDataMessage{.data = std::vector<uint8_t>(1024)});
            }
            break;
        }
    }
    void parse_message(ENetPeer* peer, const std::span<const uint8_t> message) noexcept
    {
        const messages::MessageType type = *reinterpret_cast<const messages::MessageType*>(message.data());
        switch (type)
        {
        case messages::MessageType::JoinRequest:
            if (const auto request = messages::JoinRequestMessage::deserialize(message))
            {
                const uint32_t new_id = ++client_ids;
                clients[peer].id = new_id;
                LOGI("Join request from %s accepted with id %d", request->username.c_str(), new_id);
                send_message(peer, ENET_PACKET_FLAG_RELIABLE, messages::JoinResponseMessage{
                    .accepted = true,
                    .new_id = new_id
                });
            }
            break;
        case messages::MessageType::PlayerState:
            if (const auto update = messages::PlayerStateMessage::deserialize(message))
            {
                clients[peer].position = update->position;
                clients[peer].rotation = update->rotation;
                clients[peer].velocity = update->velocity;
                //LOGI("received position: %f %f %f",
                //    update->position.x, update->position.y, update->position.z);
                // send update to all other players
                for (const auto& [send_peer, player] : clients)
                {
                    if (peer != send_peer)
                    {
                        send_message(send_peer, 0, update.value());
                    }
                }
            }
            break;
        case messages::MessageType::BlockAction:
            if (const auto block = messages::BlockActionMessage::deserialize(message))
            {
                LOGI("received block action: %d", block->action);
                if (on_block_action)
                {
                    on_block_action(*block);
                }
            }
            break;
        case messages::MessageType::ChunkData:
            if (const auto chunk = messages::ChunkDataMessage::deserialize(message))
            {
                LOGI("received chunk request for [%d, %d, %d]",
                    chunk->sector.x, chunk->sector.y, chunk->sector.z);
                on_chunk_data_request(peer, chunk.value());
            }
        }
    }
    void update(const float dt, const vk::utils::FrameContext& frame, const glm::mat4 view) noexcept
    {
        static float update_timer = 0.0f;
        update_timer += dt;
        if (server && update_timer > 0.03f)
        {
            update_timer = 0.0f;
            for (auto& [peer, player] : clients)
            {
                send_message(peer, 0, messages::PlayerStateMessage{
                    .id = player_id,
                    .position = player_pos,
                    .rotation = player_rot,
                    .velocity = player_vel,
                });
            }
            //LOGI("send position: %f %f %f", player_pos.x, player_pos.y, player_pos.z);
        }

        // basic motion interpolation
        for (auto& player : std::views::values(clients))
        {
            player.position += player.velocity * dt;
        }

        for (auto& player : removed_players)
        {
            globals::m_resources->destroy_geometry(player.cube, frame.timeline_value);
            player.destroy();
        }
        removed_players.clear();
    }
    void tick(const float dt) noexcept
    {
        ENetEvent event{};
        if (enet_host_service(server, &event, 0) > 0)
        {
            switch (event.type)
            {
            case ENET_EVENT_TYPE_CONNECT:
                LOGI("A new client connected from %s.", address2str(event.peer->address).c_str());
                // Store any relevant client information here.
                // event.peer->data = new player::PlayerState;
                add_player(event.peer);
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                //LOGI("A packet of length %llu containing %s was received from %s on channel %u.",
                //    event.packet->dataLength,
                //    reinterpret_cast<const char*>(event.packet->data),
                //    static_cast<const char*>(event.peer->data),
                //    event.channelID);
                parse_message(event.peer, {event.packet->data, event.packet->dataLength});
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                LOGI("%s disconnected.", static_cast<const char*>(event.peer->data));
                remove_player(event.peer);
                break;
            case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
                LOGI("%s disconnected due to timeout.", static_cast<const char*>(event.peer->data));
                remove_player(event.peer);
                break;
            case ENET_EVENT_TYPE_NONE:
                break;
            }
        }
    }
};
}
