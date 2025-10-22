module;
#include <string>
#include <cstdio>
#include <enet.h>
#include <format>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#elifdef _WIN32
#define LOGE(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

export module ce.app:client;
import :utils;
import :player;
import :messages;

export namespace ce::app::client
{
class ClientSystem : utils::NoCopy
{
    static constexpr std::string_view ServerHost = "localhost";
    static constexpr uint16_t ServerPort = 7777;
    ENetPeer* server = nullptr;
    ENetHost* client = nullptr;
    player::PlayerState* player = nullptr;
    std::string address2str(const ENetAddress& address) const noexcept
    {
        char ipStr[INET6_ADDRSTRLEN] = {0};
        // ENetAddress can contain either IPv4 or IPv6.
        const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(&address.host);
        inet_ntop(AF_INET6, &addr6->sin6_addr, ipStr, sizeof(ipStr));
        return std::format("{}:{}", ipStr, address.port);
    }
public:
    bool create_system(player::PlayerState& player) noexcept
    {
        this->player = &player;
        if (enet_initialize() != 0)
        {
            LOGE("An error occurred while initializing ENet.");
            return false;
        }
        client = enet_host_create(
            NULL, // create a client host
            1, // only allow 1 outgoing connection
            2, // allow up 2 channels to be used, 0 and 1
            0, // assume any amount of incoming bandwidth
            0  // assume any amount of outgoing bandwidth
        );
        if (!client)
        {
            LOGE("An error occurred while trying to create an ENet client host.");
            return false;
        }

        ENetAddress address{};
        ENetPeer* peer = nullptr;
        // Connect
        enet_address_set_host(&address, ServerHost.data());
        address.port = 7777;
        // Initiate the connection, allocating the two channels 0 and 1.
        peer = enet_host_connect(client, &address, 2, 0);
        if (!peer)
        {
            LOGE("No available peers for initiating an ENet connection.");
            return false;
        }
        // Wait up to 5 seconds for the connection attempt to succeed.
        ENetEvent event{};
        if (enet_host_service(client, &event, 1000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT)
        {
            server = event.peer;
            LOGI("Connection to %s succeeded.", address2str(address).c_str());
        }
        else
        {
              // Either the 5 seconds are up or a disconnect event was
              // received. Reset the peer in the event the 5 seconds
              // had run out without any significant event.
              enet_peer_reset(peer);
              LOGE("Connection to %s failed.", address2str(address).c_str());
        }
        return true;
    }
    void destroy_system() noexcept
    {
    }
    void tick(const float dt) noexcept
    {
        static float update_timer = 0.0f;
        update_timer += dt;
        if (server && update_timer > 0.3f)
        {
            update_timer = 0.0f;
            JPH::Vec3 pos{};
            JPH::Quat rot{};
            player->character->GetPositionAndRotation(pos, rot);
            const auto buffer = messages::serialize(messages::UpdatePosMessage{
                .position = glm::gtc::make_vec3(pos.mF32),
                .rotation = glm::gtc::make_quat(rot.mValue.mF32)
            });
            ENetPacket* packet = enet_packet_create(buffer.data(), buffer.size(), 0);
            enet_peer_send(server, 0, packet);
            LOGI("send position: %f %f %f", pos.GetX(), pos.GetY(), pos.GetZ());
        }

        ENetEvent event{};
        if (enet_host_service(client, &event, 0) > 0)
        {
            switch (event.type)
            {
            case ENET_EVENT_TYPE_CONNECT:
                LOGI("Connected to server %s.", address2str(event.peer->address).c_str());
                // Store any relevant client information here.
                server = event.peer;
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                LOGI("A packet of length %llu containing %s was received from %s on channel %u.",
                    event.packet->dataLength,
                    reinterpret_cast<const char*>(event.packet->data),
                    static_cast<const char*>(event.peer->data),
                    event.channelID);
                // Clean up the packet now that we're done using it.
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                LOGI("%s disconnected.", static_cast<const char*>(event.peer->data));
                // Reset the peer's client information.
                event.peer->data = nullptr;
                break;
            case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
                LOGI("%s disconnected due to timeout.", static_cast<const char*>(event.peer->data));
                // Reset the peer's client information.
                event.peer->data = nullptr;
                break;
            case ENET_EVENT_TYPE_NONE:
                break;
            }
        }
    }
};
}
