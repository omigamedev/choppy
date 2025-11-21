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
#include <miniaudio.h>
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

static constexpr uint32_t FrameSize = 480 * 4;
static constexpr uint32_t Samplerate = 48000;

struct my_data_source
{
    ma_data_source_base base;
    std::vector<float> pcm;
    ma_sound sound{};
    std::mutex mutex;
};

static void mic_data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);

static ma_result my_data_source_read(ma_data_source* pDataSource,
    void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead)
{
    // Read data here. Output in the same format returned by my_data_source_get_data_format().
    auto* source = static_cast<my_data_source*>(pDataSource);
    const std::span out = {static_cast<float*>(pFramesOut), frameCount};
    if (source->pcm.size() < FrameSize * 2)
    {
        std::ranges::fill(out, 0.f);
        return MA_SUCCESS;
    }
    std::lock_guard lock(source->mutex);
    const int32_t frames = std::min<int32_t>(frameCount, source->pcm.size());
    std::copy_n(source->pcm.data(), frames, out.begin());
    source->pcm.erase(source->pcm.begin(), source->pcm.begin() + frames);
    if (pFramesRead) *pFramesRead = frames;
    return MA_SUCCESS;
}

static ma_result my_data_source_seek(ma_data_source* pDataSource, ma_uint64 frameIndex)
{
    // Seek to a specific PCM frame here. Return MA_NOT_IMPLEMENTED if seeking is not supported.
    return MA_NOT_IMPLEMENTED;
}

static ma_result my_data_source_get_data_format(ma_data_source* pDataSource,
    ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap)
{
    // Return the format of the data here.
    if (pFormat) *pFormat = ma_format_f32;
    if (pChannels) *pChannels = 1;
    if (pSampleRate) *pSampleRate = Samplerate;
    if (pChannelMap) *pChannelMap = MA_CHANNEL_MONO;
    return MA_SUCCESS;
}

static ma_result my_data_source_get_cursor(ma_data_source* pDataSource, ma_uint64* pCursor)
{
    // Retrieve the current position of the cursor here. Return MA_NOT_IMPLEMENTED and set *pCursor to 0 if there is no notion of a cursor.
    return MA_NOT_IMPLEMENTED;
}

static ma_result my_data_source_get_length(ma_data_source* pDataSource, ma_uint64* pLength)
{
    // Retrieve the length in PCM frames here. Return MA_NOT_IMPLEMENTED and set *pLength to 0 if there is no notion of a length or if the length is unknown.
    return MA_NOT_IMPLEMENTED;
}

static ma_data_source_vtable g_my_data_source_vtable =
{
    my_data_source_read,
    my_data_source_seek,
    my_data_source_get_data_format,
    my_data_source_get_cursor,
    my_data_source_get_length
};

export namespace ce::app::client
{
class ClientSystem : utils::NoCopy
{
#ifdef _DEBUG
    // static constexpr std::string_view ServerHost = "192.168.1.60";
    static constexpr std::string_view ServerHost = "service.cubey.dev";
#else
    static constexpr std::string_view ServerHost = "service.cubey.dev";
#endif
    static constexpr uint16_t ServerPort = 7777;
    static constexpr uint16_t WebSoketPort = 7778;
    std::shared_ptr<rtc::WebSocket> ws;
    std::shared_ptr<rtc::PeerConnection> rtc_peer;
    std::shared_ptr<rtc::Track> rtc_world_track;
    std::shared_ptr<rtc::Track> rtc_mic_track;
    ma_device mic_device{};
    uint64_t mic_timestamp = 0;

    my_data_source audio_data_source{};
    OpusDecoder* world_decoder = nullptr;
    OpusEncoder* mic_encoder = nullptr;
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
        if (!connect())
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
            enet_peer_timeout(event.peer, 500, 10000, 30000);
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
            // LOGE("Connection to %s failed.", address2str(address).c_str());
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
        cleanup_rtc();
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
        enet_host_flush(client);
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
            // LOGI("web-socket connected, signaling ready");
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
                LOGI("received chunk data: %llu", chunk->data.size());
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
                    const auto sdp = j["candidate"].get<std::string>();
                    const auto mid = j["mid"].get<std::string>();
                    rtc_peer->addRemoteCandidate(rtc::Candidate(sdp, mid));
                }
                // LOGI("RTC Json: %s", json->json_string.c_str());
            }
            break;
        }
    }
    void cleanup_rtc() noexcept
    {
        if (rtc_world_track)
            rtc_world_track->close();
        rtc_world_track.reset();
        if (rtc_mic_track)
            rtc_mic_track->close();
        rtc_mic_track.reset();
        if (rtc_peer)
            rtc_peer->close();
        rtc_peer.reset();
    }
    bool connect_rtc() noexcept
    {
        LOGI("connect_rtc");
        rtc::Configuration config;
        config.iceServers.emplace_back("stun:stun.l.google.com:19302");
        rtc_peer = std::make_shared<rtc::PeerConnection>(config);
        rtc_peer->onStateChange([](const rtc::PeerConnection::State state)
        {
            // LOGI("RTC: onStateChange: %d", std::to_underlying(state));
        });
        rtc_peer->onGatheringStateChange([](const rtc::PeerConnection::GatheringState state)
        {
            // LOGI("RTC: Gathering State: %d", std::to_underlying(state));
        });
        rtc_peer->onLocalDescription([this](const rtc::Description& description)
        {
            const nlohmann::json message = {
                {"type", description.typeString()},
                {"description", std::string(description)}
            };
            // LOGI("RTC: onLocalDescription: %s", message.dump().c_str());
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
            // LOGI("RTC: onLocalCandidate: %s", message.dump().c_str());
            send_message(ENET_PACKET_FLAG_RELIABLE,
                messages::RTCJsonMessage{
                    .id = player_id,
                    .json_string = message.dump()
                });
        });

        create_rtc_world_track();
        create_rtc_mic_track();

        //auto offer = rtc_peer->createOffer();
        rtc_peer->setLocalDescription();
        return true;
    }
    void create_rtc_world_track() noexcept
    {
        const std::string audio_cname = "audio-track";
        rtc::Description::Audio audio{audio_cname, rtc::Description::Direction::RecvOnly};
        audio.addOpusCodec(111);
        audio.addSSRC(1, audio_cname, "stream1");
        rtc_world_track = rtc_peer->addTrack(static_cast<rtc::Description::Media>(audio));
        const auto depacketizer = std::make_shared<rtc::OpusRtpDepacketizer>();
        rtc_world_track->setMediaHandler(depacketizer);
        rtc_world_track->onClosed([this]
        {
            opus_decoder_destroy(world_decoder);
            world_decoder = nullptr;
            ma_sound_start(&audio_data_source.sound);
            ma_data_source_uninit(&audio_data_source.base);
            ma_sound_uninit(&audio_data_source.sound);
            // audio_dump.close();
        });
        rtc_world_track->onOpen([this]
        {
            // LOGI("RTC: Audio Track onOpen");
            int error = 0;
            world_decoder = opus_decoder_create(Samplerate, 1, &error);
            // audio_dump.open(std::format("audio-{}.pcm", player_id), std::ios::binary);

            ma_data_source_config baseConfig = ma_data_source_config_init();
            baseConfig.vtable = &g_my_data_source_vtable;
            if (const ma_result result = ma_data_source_init(&baseConfig, &audio_data_source.base);
                result != MA_SUCCESS)
            {
                LOGE("FAILED ma_data_source_init: %s", ma_result_description(result));
            }
            if (const ma_result result = ma_sound_init_from_data_source(&globals::audio_engine,
                &audio_data_source.base, MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_LOOPING,
                nullptr, &audio_data_source.sound);
                result != MA_SUCCESS)
            {
                LOGE("FAILED ma_sound_init_from_data_source: %s", ma_result_description(result));
            }
            ma_sound_start(&audio_data_source.sound);
        });
        rtc_world_track->onFrame([this](const rtc::binary& data, const rtc::FrameInfo& frame)
        {
            std::vector<float> pcm(FrameSize);
            const int samples = opus_decode_float(world_decoder, reinterpret_cast<const uint8_t*>(data.data()),
                static_cast<opus_int32>(data.size()), pcm.data(), static_cast<int32_t>(pcm.size()), 0);
            std::lock_guard lock(audio_data_source.mutex);
            if (audio_data_source.pcm.size() > FrameSize * 10)
                audio_data_source.pcm.erase(audio_data_source.pcm.begin(), audio_data_source.pcm.begin() + FrameSize * 5);
            audio_data_source.pcm.append_range(pcm);
            // LOGI("RTC: onFrame %llu bytes to %d samples", data.size(), samples);
            // audio_dump.write(reinterpret_cast<const char*>(pcm.data()), pcm.size() * sizeof(float));
        });
    }
    void create_rtc_mic_track() noexcept
    {
        const std::string audio_cname = "mic-track";
        rtc::Description::Audio audio{audio_cname, rtc::Description::Direction::SendOnly};
        audio.addOpusCodec(111);
        audio.addSSRC(2, audio_cname, "stream1");
        rtc_mic_track = rtc_peer->addTrack(static_cast<rtc::Description::Media>(audio));
        auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(2, audio_cname, 111,
            rtc::OpusRtpPacketizer::DefaultClockRate);
        const auto packetizer = std::make_shared<rtc::OpusRtpPacketizer>(rtpConfig);
        const auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig);
        packetizer->addToChain(srReporter);
        const auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
        packetizer->addToChain(nackResponder);
        rtc_mic_track->setMediaHandler(packetizer);
        rtc_mic_track->onClosed([this]
        {
            opus_encoder_destroy(mic_encoder);
            mic_encoder = nullptr;
            ma_device_stop(&mic_device);
            ma_device_uninit(&mic_device);
        });
        rtc_mic_track->onOpen([this]
        {
            // LOGI("RTC: Audio Track onOpen");
            int error = 0;
            mic_encoder = opus_encoder_create(Samplerate, 1, OPUS_APPLICATION_VOIP, &error);

            ma_device_config config = ma_device_config_init(ma_device_type_capture);
            // config.capture.pDeviceID = &pCaptureDeviceInfos[2].id;
            config.capture.format   = ma_format_f32;   // Set to ma_format_unknown to use the device's native format.
            config.capture.channels = 1;               // Set to 0 to use the device's native channel count.
            // config.capture.shareMode = ma_share_mode_shared;
            // config.wasapi.usage = ma_wasapi_usage_pro_audio;
            config.noPreSilencedOutputBuffer = false;
            config.periodSizeInFrames = FrameSize;
            config.sampleRate        = Samplerate;           // Set to 0 to use the device's native sample rate.
            config.dataCallback      = mic_data_callback;   // This function will be called when miniaudio needs more data.
            config.pUserData         = this;   // Can be accessed from the device object (device.pUserData).
            if (const ma_result result = ma_device_init(nullptr, &config, &mic_device); result != MA_SUCCESS)
            {
                LOGE("FAILED ma_device_init: %s", ma_result_description(result));
                return;
            }
            ma_device_start(&mic_device);     // The device is sleeping by default so you'll need to start it manually.
        });
    }
    void update(const float dt, const vk::utils::FrameContext& frame, const glm::mat4& view) noexcept
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
    void send_mic_data(const std::span<const float> pcm) noexcept
    {
        if (rtc_mic_track && rtc_mic_track->isOpen())
        {
            std::vector<uint8_t> packet(size_t{4000});
            const int result = opus_encode_float(mic_encoder, pcm.data(),
                pcm.size(), packet.data(), packet.size());
            if (result > 0)
            {
                const auto timestamp = std::chrono::duration<double>(
                    static_cast<double>(mic_timestamp) / static_cast<double>(Samplerate));
                rtc_mic_track->sendFrame(reinterpret_cast<const std::byte*>(packet.data()),
                    result, rtc::FrameInfo{timestamp});
            }
        }
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
        while (enet_host_service(client, &event, 0) > 0)
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
                cleanup_rtc();
                connect_async();
                break;
            case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
                LOGI("%s disconnected due to timeout.", static_cast<const char*>(event.peer->data));
                server = nullptr;
                removed_players.append_range(std::views::values(players));
                players.clear();
                if (ws && ws->isOpen())
                    ws->close();
                cleanup_rtc();
                connect_async();
                break;
            case ENET_EVENT_TYPE_NONE:
                break;
            }
        }
    }
};
}

static void mic_data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    // In playback mode copy data to pOutput. In capture mode read data from pInput. In full-duplex mode, both
    // pOutput and pInput will be valid and you can move data from pInput into pOutput. Never process more than
    // frameCount frames.
    auto* context = static_cast<ce::app::client::ClientSystem*>(pDevice->pUserData);
    const auto data = std::span(static_cast<const float*>(pInput), frameCount);
    context->send_mic_data(data);
    // LOGI("mic_data_callback");
}
