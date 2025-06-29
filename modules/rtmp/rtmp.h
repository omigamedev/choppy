#pragma once
#include <vector>
#include <thread>
#include <string>
#include <cstdint>
#include <array>
#include <ranges>
#include <condition_variable>
#include <chrono>
#include <bit>
#include <map>

#include "amf.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#endif

namespace rtmp
{
    enum class ChunkType : uint8_t
    {
        Type0 = 0x00, // Full header
        Type1 = 0x01, // No stream ID
        Type2 = 0x02, // Only timestamp delta
        Type3 = 0x03, // Only payload
    };
    enum class MessageType : uint8_t
    {
        SetChunkSize = 0x01,
        AbortMessage = 0x02,
        Acknowledgement = 0x03,
        ControlMessage = 0x04,
        WindowAcknowledgementSize = 0x05,
        SetPeerBandwidth = 0x06,
        VirtualControl = 0x07,
        AudioPacket = 0x08,
        VideoPacket = 0x09,
        DataExtended = 0x0F,
        ContainerExtended = 0x10,
        CommandAMF3 = 0x11,
        Data = 0x12,
        Container = 0x13,
        CommandAMF0 = 0x14,
        UDP = 0x15,
        Aggregate = 0x16,
        Present = 0x17,
    };
    std::string to_string(MessageType t) noexcept;
    struct BasicHeader
    {
        uint8_t stream_id : 6 = 3;
        ChunkType header_type : 2 = ChunkType::Type0;
    };
    struct FullHeader
    {
        BasicHeader basic_header;
        std::array<uint8_t, 3> timestamp = { 0, 0, 0 };
        std::array<uint8_t, 3> message_size = { 0, 0, 0 };
        MessageType message_type = MessageType::CommandAMF0;
        uint32_t message_streamd_id = 0; // little-endian
        [[nodiscard]] inline uint32_t size() const noexcept
        {
            return from_big_endian<uint32_t>(message_size);
        }
        [[nodiscard]] inline uint32_t ts() const noexcept
        {
            return from_big_endian<uint32_t>(timestamp);
        }
        [[nodiscard]] inline auto bytes() const noexcept
        {
            return std::bit_cast<std::array<uint8_t, sizeof(FullHeader)>>(*this);
        }
    };
    struct Type01Header
    {
        BasicHeader basic_header { .header_type = ChunkType::Type1 };
        std::array<uint8_t, 3> timestamp = { 0, 0, 0 };
        std::array<uint8_t, 3> message_size = { 0, 0, 0 };
        MessageType message_type = MessageType::CommandAMF0;
        [[nodiscard]] inline uint32_t size() const noexcept
        {
            return from_big_endian<uint32_t>(message_size);
        }
        [[nodiscard]] inline uint32_t ts() const noexcept
        {
            return from_big_endian<uint32_t>(timestamp);
        }
        [[nodiscard]] inline auto bytes() const noexcept
        {
            return std::bit_cast<std::array<uint8_t, sizeof(Type01Header)>>(*this);
        }
    };
    struct Type2Header
    {
        BasicHeader basic_header{ .header_type = ChunkType::Type2 };
        std::array<uint8_t, 3> timestamp = { 0, 0, 0 };
        [[nodiscard]] inline uint32_t ts() const noexcept
        {
            return from_big_endian<uint32_t>(timestamp);
        }
        [[nodiscard]] inline auto bytes() const noexcept
        {
            return std::bit_cast<std::array<uint8_t, sizeof(Type2Header)>>(*this);
        }
    };
    struct Packet
    {
        FullHeader header;
        amf::Message body;
        void update_header() noexcept
        {
            header.message_size = trunc_be<3>(to_big_endian(static_cast<uint32_t>(body.size())));
        }
        [[nodiscard]] std::vector<uint8_t> bytes() const noexcept
        {
            std::vector<uint8_t> out;
            out.append_range(header.bytes());
            out.append_range(body.data());
            return out;
        }
    };
    struct Packet01
    {
        Type01Header header;
        amf::Message body;
        void update_header() noexcept
        {
            header.message_size = trunc_be<3>(to_big_endian(static_cast<uint32_t>(body.size())));
        }
        [[nodiscard]] std::vector<uint8_t> bytes() const noexcept
        {
            std::vector<uint8_t> out;
            out.append_range(header.bytes());
            out.append_range(body.data());
            return out;
        }
    };
    class Socket
    {
#if defined(_WIN32)
        SOCKET sockfd = NULL;
#else
        int sockfd = -1;
#endif
        std::thread receive_thread;
        std::map<uint8_t, FullHeader> last_header;
        std::map<uint8_t, std::vector<uint8_t>> stream_buffer;
        std::condition_variable result_cv;
        std::mutex mutex;
        std::string m_host;
        uint32_t packet_max_size = 128;
        void parse_amf0(const std::vector<uint8_t>& buffer);
        void dump(auto& data) const noexcept;
        void dump_data(std::span<uint8_t> data) const noexcept;
        void parse_chunk_size(std::span<uint8_t> data);
        void parse_window_ack(std::span<uint8_t> data);
        void parse_client_bw(std::span<uint8_t> data);
        void receive_loop();
    public:
        bool connect_host(std::string_view host, int32_t port) noexcept;
        void close();
        bool handshake() noexcept;
        void wait_result();
        void start_receiving();
        void send_data(std::span<uint8_t> data) const noexcept;
        template<typename T>
        bool send_packet(const T& packet) const noexcept
        {
            auto data = packet.bytes();
            send_data(data);
            return true;
        }
        template<typename T>
        bool send_packet_with_data(const T& packet, std::span<uint8_t> raw_data) const noexcept
        {
            auto data = packet.bytes();
            data.append_range(raw_data);
            send_data(data);
            return true;
        }
        bool send_packets(const std::vector<Packet>& packets) const noexcept;
        void send_chunk_size();
        void send_connect_command(std::string_view app);
        void send_create_stream_command(std::string_view key);
        void send_publish_command(std::string_view key);
        void send_close_command(std::string_view key);
        void send_audio_header(std::span<const uint8_t> aac_config);
        void send_audio_aac(std::span<const uint8_t> aac_raw, uint32_t ts);
        void send_video_header(std::span<const uint8_t> sps, std::span<const uint8_t> pps);
        void send_video_h264(std::vector<std::span<const uint8_t>> nals, uint32_t ts, bool keyframe);
    };
}
