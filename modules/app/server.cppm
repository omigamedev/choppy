module;
#include <ranges>
#include <string>
#include <cstdio>
#include <enet.h>
#include <format>
#include <unordered_map>
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

export module ce.app:server;
import :utils;
import :player;
import :resources;
import :physics;
import :messages;
import ce.shaders.solidcolor;
import ce.vk.utils;

static constexpr uint32_t FrameSize = 480 * 4;
static constexpr uint32_t Samplerate = 48000;

export namespace ce::app::server
{
struct RTCPeer
{
    std::shared_ptr<rtc::PeerConnection> peer;
    std::shared_ptr<rtc::Track> audio_track;
    std::shared_ptr<rtc::Track> mic_track;
    std::shared_ptr<rtc::DataChannel> data_channel;
    std::chrono::duration<double> timestamp{0};
    bool connected = false;
    OpusEncoder* enc = nullptr;
    OpusDecoder* dec = nullptr;
    uint64_t encoded_samples = 0;
    std::ofstream audio_dump;
    std::vector<std::vector<float>> mic_buffer;
    std::vector<std::vector<float>> audio_mix_buffer;
};
class ServerSystem : utils::NoCopy
{
    static constexpr uint32_t MaxClients = 32;
    std::shared_ptr<rtc::WebSocketServer> wss;
    std::vector<std::shared_ptr<rtc::WebSocket>> ws_clients;
    std::mutex audio_mutex;
    ENetHost* server = nullptr;
    uint32_t client_ids = 1;
    std::unordered_map<ENetPeer*, player::PlayerState> clients;
    std::unordered_map<ENetPeer*, RTCPeer> rtc_peers;
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
        enet_host_flush(server);
        //LOGI("sent message of type: %s", messages::to_string(message.type));
    }
    template<typename T>
    void broadcast_message(const uint32_t enet_flags, const T& message) const noexcept
    {
        const auto buffer = message.serialize();
        ENetPacket* packet = enet_packet_create(buffer.data(), buffer.size(), enet_flags);
        for (ENetPeer* peer : std::views::keys(clients))
            enet_peer_send(peer, 0, packet);
        enet_host_flush(server);
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

        if (rtc_peers[peer].audio_track)
        {
            rtc_peers[peer].audio_track->close();
        }
        if (rtc_peers[peer].data_channel)
        {
            rtc_peers[peer].data_channel->close();
        }
        if (rtc_peers[peer].peer)
        {
            rtc_peers[peer].peer->close();
            rtc_peers.erase(peer);
        }

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
                //connect_rtc(peer);
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
                //LOGI("received chunk request for [%d, %d, %d]",
                //    chunk->sector.x, chunk->sector.y, chunk->sector.z);
                LOGI("received request for %llu chunks", chunk->sectors.size());
                on_chunk_data_request(peer, chunk.value());
            }
            break;
        case messages::MessageType::RTCJson:
            if (const auto json = messages::RTCJsonMessage::deserialize(message))
            {
                const nlohmann::json j = nlohmann::json::parse(json->json_string);
                const auto sdp_type = j["type"].get<std::string>();
                // LOGI("RTC Json: %s", json->json_string.c_str());
                if (sdp_type == "offer")
                {
                    connect_rtc(peer);
                    const auto sdp = j["description"].get<std::string>();
                    rtc_peers[peer].peer->setRemoteDescription(rtc::Description(sdp, sdp_type));
                }
                else if (sdp_type == "candidate")
                {
                    const auto sdp = j["candidate"].get<std::string>();
                    const auto mid = j["mid"].get<std::string>();
                    rtc_peers[peer].peer->addRemoteCandidate(rtc::Candidate(sdp, mid));
                }
                else
                {
                    LOGE("shouldn't happen");
                }
            }
            break;
        }
    }
    bool connect_rtc(ENetPeer* peer) noexcept
    {
        LOGI("connect_rtc");
        rtc::Configuration config;
        config.iceServers.emplace_back("stun:stun.l.google.com:19302");
        config.portRangeBegin = 7780;
        config.portRangeEnd = 7790;
        auto rtc_peer = std::make_shared<rtc::PeerConnection>(config);
        rtc_peers.emplace(peer, RTCPeer{.peer = rtc_peer});
        rtc_peer->onStateChange([this, peer](const rtc::PeerConnection::State state)
        {
            // LOGI("RTC: onStateChange: %d", std::to_underlying(state));
            if (state == rtc::PeerConnection::State::Connected)
            {
                rtc_peers[peer].connected = true;
            }
            if (state == rtc::PeerConnection::State::Closed)
            {
                // LOGI("RTC: onStateChange: Closed");
                rtc_peers[peer].connected = false;
            }
        });
        rtc_peer->onGatheringStateChange([](const rtc::PeerConnection::GatheringState state)
        {
            // LOGI("RTC: Gathering State: %d", std::to_underlying(state));
        });
        rtc_peer->onLocalDescription([this, peer](const rtc::Description& description)
        {
            const nlohmann::json message = {
                {"type", description.typeString()},
                {"description", std::string(description)}
            };
            // LOGI("RTC: onLocalDescription: %s", message.dump().c_str());
            send_message(peer, ENET_PACKET_FLAG_RELIABLE,
                messages::RTCJsonMessage{
                    .json_string = message.dump()
                });
        });
        rtc_peer->onLocalCandidate([this, peer](const rtc::Candidate& candidate)
        {
            const nlohmann::json message = {
                {"type", "candidate"},
                {"candidate", std::string(candidate)},
                {"mid", candidate.mid()}
            };
            // LOGI("RTC: onLocalCandidate: %s", message.dump().c_str());
            send_message(peer, ENET_PACKET_FLAG_RELIABLE,
                messages::RTCJsonMessage{
                    .json_string = message.dump()
                });
        });
        rtc_peer->onTrack([this, peer](const std::shared_ptr<rtc::Track>& track)
        {
            // LOGI("RTC: onTrack: %s", track->mid().c_str());
            // create RTP configuration
            if (track->mid() == "audio-track")
            {
                auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(1, track->mid(), 111,
                    rtc::OpusRtpPacketizer::DefaultClockRate);
                const auto packetizer = std::make_shared<rtc::OpusRtpPacketizer>(rtpConfig);
                const auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig);
                packetizer->addToChain(srReporter);
                const auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
                packetizer->addToChain(nackResponder);
                track->setMediaHandler(packetizer);
                rtc_peers[peer].audio_track = track;
                int error = 0;
                rtc_peers[peer].enc = opus_encoder_create(Samplerate, 1, OPUS_APPLICATION_VOIP, &error);
            }
            else if (track->mid() == "mic-track")
            {
                const auto depacketizer = std::make_shared<rtc::OpusRtpDepacketizer>();
                track->setMediaHandler(depacketizer);
                track->onOpen([this, peer]
                {
                    int error = 0;
                    rtc_peers[peer].dec = opus_decoder_create(Samplerate, 1, &error);
                    // rtc_peers[peer].audio_dump.open(
                    //     std::format("audio-{}.pcm", clients[peer].id), std::ios::binary);
                });
                track->onFrame([this, peer](const rtc::binary& data, const rtc::FrameInfo& frame)
                {
                    // LOGI("RTC: onFrame");
                    std::vector<float> pcm(FrameSize);
                    const int samples = opus_decode_float(rtc_peers[peer].dec, reinterpret_cast<const uint8_t*>(data.data()),
                        static_cast<opus_int32>(data.size()), pcm.data(), static_cast<int32_t>(pcm.size()), 0);
                    // rtc_peers[peer].audio_dump.write(reinterpret_cast<const char*>(pcm.data()),
                    //     pcm.size() * sizeof(float));
                    std::lock_guard audio_lock(audio_mutex);
                    rtc_peers[peer].mic_buffer.push_back(std::move(pcm));
                });
                track->onClosed([this, peer]
                {
                    opus_decoder_destroy(rtc_peers[peer].dec);
                    // rtc_peers[peer].audio_dump.close();
                });
                rtc_peers[peer].mic_track = track;
            }
        });
        return true;
    }
    void update(const float dt, const vk::utils::FrameContext& frame, const glm::mat4& view) noexcept
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
    void audio_mixdown() noexcept
    {
        std::lock_guard audio_lock(audio_mutex);
        auto ready_peers = std::views::values(rtc_peers) |
            std::views::filter([](const auto& peer){ return peer.connected && peer.audio_track->isOpen(); });
        if (ready_peers.empty())
            return;
        const auto [min_frames, max_frames] = std::ranges::minmax(ready_peers |
            std::views::transform([](const auto& peer){ return peer.mic_buffer.size(); }));
        const auto frames_to_send = (max_frames - min_frames > 5) ? max_frames : min_frames;
        if (frames_to_send == 0)
            return;
        std::vector<uint8_t> packet_buffer(size_t{4000});
        for (auto& [peer, rtc_peer] : rtc_peers)
        {
            if (!rtc_peer.connected || !rtc_peer.audio_track->isOpen())
                continue;

            std::vector<std::vector<float>> frames;
            for (size_t i = 0; i < frames_to_send; ++i)
                frames.emplace_back(FrameSize);
            for (const auto& [other_peer, other_rtc_peer] : rtc_peers)
            {
                if (peer != other_peer)
                {
                    const size_t frames_to_copy = std::min(frames_to_send, other_rtc_peer.mic_buffer.size());
                    for (size_t i = 0; i < frames_to_copy; ++i)
                    {
                        std::ranges::transform(frames[i], other_rtc_peer.mic_buffer[i],
                            frames[i].begin(), std::plus{});
                    }
                }
            }
            for (const auto& frame : frames)
            {
                const double audio_time = static_cast<double>(rtc_peer.encoded_samples) / static_cast<double>(Samplerate);
                const int result = opus_encode_float(rtc_peer.enc, frame.data(),
                    frame.size(), packet_buffer.data(), packet_buffer.size());
                if (result > 0)
                {
                    const auto timestamp = std::chrono::duration<double>{audio_time};
                    rtc_peer.audio_track->sendFrame(reinterpret_cast<const std::byte*>(packet_buffer.data()),
                        result, rtc::FrameInfo{timestamp});
                }
                rtc_peer.encoded_samples += frame.size();
            }
        }

        // remove processed frames
        for (auto& rtc_peer : rtc_peers | std::views::values)
        {
            const auto frames_to_delete = std::min(rtc_peer.mic_buffer.size(), frames_to_send);
            rtc_peer.mic_buffer.erase(rtc_peer.mic_buffer.begin(),
               rtc_peer.mic_buffer.begin() + frames_to_delete);
        }
    }
    void tick(const float dt) noexcept
    {
        if (!rtc_peers.empty())
            audio_mixdown();

        ENetEvent event{};
        while (enet_host_service(server, &event, 10) > 0)
        {
            switch (event.type)
            {
            case ENET_EVENT_TYPE_CONNECT:
                LOGI("A new client connected from %s.", address2str(event.peer->address).c_str());
                // Store any relevant client information here.
                // event.peer->data = new player::PlayerState;
                enet_peer_timeout(event.peer, 500, 10000, 30000);
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
                enet_peer_reset_queues(event.peer);
                remove_player(event.peer);
                break;
            case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
                LOGI("%s disconnected due to timeout.", static_cast<const char*>(event.peer->data));
                enet_peer_reset_queues(event.peer);
                remove_player(event.peer);
                break;
            case ENET_EVENT_TYPE_NONE:
                break;
            }
        }
    }
};
}
