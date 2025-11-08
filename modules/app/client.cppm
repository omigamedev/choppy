module;
#include <future>
#include <ranges>
#include <string>
#include <cstdio>
#include <format>
#include <unordered_map>
#include <enet.h>
#include <tracy/Tracy.hpp>
#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>
#include <opus.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#else
#define LOGE(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

export module ce.app:client;
import :utils;
import :player;
import :messages;
import ce.shaders.solidcolor;
import ce.vk.utils;

export namespace ce::app::client
{
class ClientSystem : utils::NoCopy
{
    static constexpr std::string_view ServerHost = "192.168.1.65";
    static constexpr uint16_t ServerPort = 7777;
    static constexpr uint16_t WebSoketPort = 7778;
    std::shared_ptr<rtc::WebSocket> ws;
    std::shared_ptr<rtc::PeerConnection> rtc_peer;
    std::shared_ptr<rtc::Track> rtc_track;
    OpusDecoder* decoder = nullptr;
    ENetPeer* server = nullptr;
    ENetHost* client = nullptr;
    std::unordered_map<uint32_t, player::PlayerState> players;
    std::vector<player::PlayerState> removed_players;
    std::future<bool> connect_result;
    std::ofstream audio_dump;
    bool try_connecting = false;
    [[nodiscard]] std::string address2str(const ENetAddress& address) const noexcept
    {
        char ipStr[INET6_ADDRSTRLEN] = {0};
        // ENetAddress can contain either IPv4 or IPv6.
        const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(&address.host);
        inet_ntop(AF_INET6, &addr6->sin6_addr, ipStr, sizeof(ipStr));
        return std::format("{}:{}", ipStr, address.port);
    }
public:
    uint32_t player_id = 0;
    glm::vec3 player_pos = glm::vec3(0, 0, 0);
    glm::quat player_rot = glm::gtc::identity<glm::quat>();
    glm::vec3 player_vel = glm::vec3(0, 0, 0);
    std::function<void()> on_connected;
    std::function<void(const messages::BlockActionMessage&)> on_block_action;
    std::function<void(const messages::ChunkDataMessage&)> on_chunk_data;
    [[nodiscard]] bool connected() const noexcept { return server != nullptr; }
    bool create_system() noexcept
    {
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
        connect_async();
        return true;
    }
    void connect_async() noexcept
    {
        LOGI("Connecting to server...");
        try_connecting = true;
        connect_result = std::async(std::launch::async, [this]
        {
            while (try_connecting)
            {
                if (connect())
                {
                    return true;
                }
            }
            return false;
        });
    }
    bool connect() noexcept
    {
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
        ENetEvent event{};
        if (enet_host_service(client, &event, 1000) > 0 &&
            event.type == ENET_EVENT_TYPE_CONNECT)
        {
            LOGI("Connection to %s succeeded.", address2str(address).c_str());
            server = event.peer;
            send_message(ENET_PACKET_FLAG_RELIABLE, messages::JoinRequestMessage{
                .username = std::format("random_user_{}", rand())
            });
        }
        else
        {
            // Either the 5 seconds are up or a disconnect event was
            // received. Reset the peer in the event the 5 seconds
            // had run out without any significant event.
            enet_peer_reset(peer);
            LOGE("Connection to %s failed.", address2str(address).c_str());
            return false;
        }
        return true;
    }
    void destroy_system() noexcept
    {
        for (auto& player : std::views::values(players))
        {
            globals::m_resources->destroy_geometry(player.cube, 0);
            player.destroy();
        }
        try_connecting = false;
        if (connect_result.valid())
            connect_result.get();
        if (ws && ws->isOpen())
            ws->close();
        if (server)
            enet_peer_reset(server);
        enet_host_destroy(client);
    }
    template<typename T>
    void send_message(const uint32_t enet_flags, const T& message) const noexcept
    {
        const auto buffer = message.serialize();
        ENetPacket* packet = enet_packet_create(buffer.data(), buffer.size(), enet_flags);
        enet_peer_send(server, 0, packet);
        //LOGI("sent message of type: %s", messages::to_string(message.type));
    }
    template<typename T>
    void ws_send_message(const T& message) const noexcept
    {
        const auto buffer = message.serialize();
        if (ws && ws->isOpen())
        {
            ws->send(reinterpret_cast<const std::byte*>(buffer.data()), buffer.size());
        }
    }
    [[nodiscard]] auto get_players() noexcept
    {
        return std::views::values(players);
    }
    void connect_websocket() noexcept
    {
        ws = std::make_shared<rtc::WebSocket>();
        ws->open(std::format("ws://{}:{}", ServerHost, WebSoketPort));
        ws->onOpen([this]
        {
            LOGI("web-socket connected, signaling ready");
            ws_send_message(messages::JoinResponseMessage{.new_id = player_id});
        });
        ws->onClosed([]{ LOGI("web-socket closed"); });
        ws->onError([](const std::string &error){ LOGE( "web-socket failed %s: ", error.c_str()); });
        ws->onMessage([&](const rtc::message_variant& data){
            LOGI("web-socket on message");
            if (std::holds_alternative<rtc::binary>(data))
            {
                const auto& bin = std::get<rtc::binary>(data);
                ws_parse_message({reinterpret_cast<const uint8_t*>(bin.data()), bin.size()});
            }
            else
            {
                LOGE("web-socket on message: string data %s", std::get<std::string>(data).c_str());
            }
        });
    }
    void ws_parse_message(std::span<const uint8_t> message) noexcept
    {
        const messages::MessageType type = *reinterpret_cast<const messages::MessageType*>(message.data());
        switch (type)
        {
        case messages::MessageType::WorldData:
            if (const auto world_data = messages::WorldDataMessage::deserialize(message))
            {
                LOGI("RECEIVED WORLD DATA of %llu bytes", world_data->data.size());
            }
            break;
        }
    }
    void parse_message(ENetPeer* peer, const std::span<const uint8_t> message) noexcept
    {
        const messages::MessageType type = *reinterpret_cast<const messages::MessageType*>(message.data());
        switch (type)
        {
        case messages::MessageType::JoinResponse:
            if (const auto response = messages::JoinResponseMessage::deserialize(message))
            {
                if (response->accepted)
                {
                    player_id = response->new_id;
                    LOGI("join accepted with id: %d", player_id);
                    connect_websocket();
                    connect_rtc();
                }
                else
                {
                    LOGE("join NOT accepted, sad");
                }
            }
            break;
        case messages::MessageType::PlayerRemoved:
            if (const auto removed = messages::PlayerRemovedMessage::deserialize(message))
            {
                removed_players.emplace_back(players[removed->id]);
                players.erase(removed->id);
            }
            break;
        case messages::MessageType::PlayerState:
            if (const auto update = messages::PlayerStateMessage::deserialize(message))
            {
                if (players.contains(update->id))
                {
                    auto& player = players[update->id];
                    player.position = update->position;
                    player.rotation = update->rotation;
                    player.velocity = update->velocity;
                }
                else
                {
                    player::PlayerState player{
                        .id = update->id,
                        .cube = globals::m_resources->create_cube<shaders::SolidColorShader>(),
                        .position = update->position,
                        .rotation = update->rotation,
                        .velocity = update->velocity,
                    };
                    players.emplace(std::pair(update->id, player));
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
                //LOGI("received chunk data: %d", (int)chunk->data.size());
                on_chunk_data(chunk.value());
            }
            break;
        case messages::MessageType::RTCJson:
            if (const auto json = messages::RTCJsonMessage::deserialize(message))
            {
                const nlohmann::json j = nlohmann::json::parse(json->json_string);
                const auto sdp_type = j["type"].get<std::string>();
                if (sdp_type == "answer")
                {
                    //connect_rtc();
                    const auto sdp = j["description"].get<std::string>();
                    rtc_peer->setRemoteDescription(rtc::Description(sdp, sdp_type));
                }
                else if (sdp_type == "candidate")
                {
                    auto sdp = j["candidate"].get<std::string>();
                    auto mid = j["mid"].get<std::string>();
                    rtc_peer->addRemoteCandidate(rtc::Candidate(sdp, mid));
                }
                LOGI("RTC Json: %s", json->json_string.c_str());
            }
            break;
        }
    }
    bool connect_rtc() noexcept
    {
        LOGI("connect_rtc");
        rtc::Configuration config;
        config.iceServers.emplace_back("stun:stun.l.google.com:19302");
        rtc_peer = std::make_shared<rtc::PeerConnection>(config);
        rtc_peer->onStateChange([](const rtc::PeerConnection::State state)
        {
            LOGI("RTC: onStateChange: %d", std::to_underlying(state));
        });
        rtc_peer->onGatheringStateChange([](const rtc::PeerConnection::GatheringState state)
        {
            LOGI("RTC: Gathering State: %d", std::to_underlying(state));
        });
        rtc_peer->onLocalDescription([this](const rtc::Description& description)
        {
            const nlohmann::json message = {
                {"type", description.typeString()},
                {"description", std::string(description)}
            };
            LOGI("RTC: onLocalDescription: %s", message.dump().c_str());
            send_message(ENET_PACKET_FLAG_RELIABLE,
                messages::RTCJsonMessage{
                    .id = player_id,
                    .json_string = message.dump()
                });
        });
        rtc_peer->onLocalCandidate([this](const rtc::Candidate& candidate)
        {
            const nlohmann::json message = {
                {"type", "candidate"},
                {"candidate", std::string(candidate)},
                {"mid", candidate.mid()}
            };
            LOGI("RTC: onLocalCandidate: %s", message.dump().c_str());
            send_message(ENET_PACKET_FLAG_RELIABLE,
                messages::RTCJsonMessage{
                    .id = player_id,
                    .json_string = message.dump()
                });
        });

        const std::string audio_cname = "audio-track";
        rtc::Description::Audio audio{audio_cname, rtc::Description::Direction::SendRecv};
        audio.addOpusCodec(111);
        audio.addSSRC(1, audio_cname, "stream1");
        rtc_track = rtc_peer->addTrack(static_cast<rtc::Description::Media>(audio));
        auto depacketizer = std::make_shared<rtc::OpusRtpDepacketizer>();
        rtc_track->setMediaHandler(depacketizer);
        rtc_track->onOpen([this]
        {
            LOGI("RTC: Audio Track onOpen");
            int error = 0;
            decoder = opus_decoder_create(48000, 1, &error);
            audio_dump.open("audio.pcm", std::ios::binary);
        });
        rtc_track->onFrame([this](const rtc::binary& data, const rtc::FrameInfo& frame)
        {
            std::vector<float> pcm(480);
            int samples = opus_decode_float(decoder, reinterpret_cast<const uint8_t*>(data.data()),
                data.size(), pcm.data(), pcm.size(), 0);
            LOGI("RTC: onFrame %llu bytes to %d samples", data.size(), samples);
            audio_dump.write(reinterpret_cast<const char*>(pcm.data()), pcm.size() * sizeof(float));
        });
        //auto offer = rtc_peer->createOffer();
        rtc_peer->setLocalDescription();
        return true;
    }
    void update(const float dt, const vk::utils::FrameContext& frame, const glm::mat4 view) noexcept
    {
        // basic motion interpolation
        for (auto& player : std::views::values(players))
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
        static float update_timer = 0.0f;
        update_timer += dt;
        if (server && update_timer > 0.03f && player_id > 0)
        {
            update_timer = 0.0f;
            send_message(0, messages::PlayerStateMessage{
                .id = player_id,
                .position = player_pos,
                .rotation = player_rot,
                .velocity = player_vel,
            });
            //LOGI("send position: %f %f %f", player_pos.x, player_pos.y, player_pos.z);
        }

        ENetEvent event{};
        if (enet_host_service(client, &event, 0) > 0)
        {
            switch (event.type)
            {
            case ENET_EVENT_TYPE_CONNECT:
                // not happening here, checkout connect() function
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                parse_message(event.peer, {event.packet->data, event.packet->dataLength});
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                LOGI("%s disconnected.", static_cast<const char*>(event.peer->data));
                server = nullptr;
                removed_players.append_range(std::views::values(players));
                players.clear();
                if (ws && ws->isOpen())
                    ws->close();
                connect_async();
                break;
            case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
                LOGI("%s disconnected due to timeout.", static_cast<const char*>(event.peer->data));
                server = nullptr;
                removed_players.append_range(std::views::values(players));
                players.clear();
                if (ws && ws->isOpen())
                    ws->close();
                connect_async();
                break;
            case ENET_EVENT_TYPE_NONE:
                break;
            }
        }
    }
};
}
