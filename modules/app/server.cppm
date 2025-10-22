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

export module ce.app:server;
import :utils;
import :player;

export namespace ce::app::server
{
class ServerSystem : utils::NoCopy
{
    static constexpr uint32_t MaxClients = 32;
    ENetHost* server = nullptr;
    std::unordered_map<ENetPeer*, player::PlayerState> clients;
    std::string address2str(const ENetAddress& address) const noexcept
    {
        char ipStr[INET6_ADDRSTRLEN] = {0};
        // ENetAddress can contain either IPv4 or IPv6.
        const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(&address.host);
        inet_ntop(AF_INET6, &addr6->sin6_addr, ipStr, sizeof(ipStr));
        return std::format("{}:{}", ipStr, address.port);
    }
public:
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
        return true;
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
                clients.emplace(std::pair(event.peer, player::PlayerState{}));
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
                clients.erase(event.peer);
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
    void destroy_system() noexcept
    {
        if (server)
        {
            enet_host_destroy(server);
            server = nullptr;
        }
        enet_deinitialize();
    }
};
}