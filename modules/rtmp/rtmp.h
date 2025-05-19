#pragma once
#include <iostream>
#include <vector>
#include <thread>
#include <string>
#include <cstdint>
#include <cstring>
#include <array>
#include <ranges>
#include <condition_variable>
#include <algorithm>
#include <optional>
#include <variant>
#include <chrono>
#include <print>
#include <bit>
#include <map>
#include <fstream>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include "amf.h"

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
    std::string to_string(MessageType t) noexcept
    {
        switch (t)
        {
            case rtmp::MessageType::SetChunkSize: return "SetChunkSize";
            case rtmp::MessageType::AbortMessage: return "AbortMessage";
            case rtmp::MessageType::Acknowledgement: return "Acknowledgement";
            case rtmp::MessageType::ControlMessage: return "ControlMessage";
            case rtmp::MessageType::WindowAcknowledgementSize: return "WindowAcknowledgementSize";
            case rtmp::MessageType::SetPeerBandwidth: return "SetPeerBandwidth";
            case rtmp::MessageType::VirtualControl: return "VirtualControl";
            case rtmp::MessageType::AudioPacket: return "AudioPacket";
            case rtmp::MessageType::VideoPacket: return "VideoPacket";
            case rtmp::MessageType::DataExtended: return "DataExtended";
            case rtmp::MessageType::ContainerExtended: return "ContainerExtended";
            case rtmp::MessageType::CommandAMF3: return "CommandAMF3";
            case rtmp::MessageType::Data: return "Data";
            case rtmp::MessageType::Container: return "Container";
            case rtmp::MessageType::CommandAMF0: return "CommandAMF0";
            case rtmp::MessageType::UDP: return "UDP";
            case rtmp::MessageType::Aggregate: return "Aggregate";
            case rtmp::MessageType::Present: return "Present";
            default:
                return std::format("Unknown 0x{:0x}", (uint8_t)t);
        }
    }
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
        uint32_t size() const noexcept
        {
            return from_big_endian<uint32_t>(message_size);
        }
        uint32_t ts() const noexcept
        {
            return from_big_endian<uint32_t>(timestamp);
        }
        auto bytes() const noexcept
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
        uint32_t size() const noexcept
        {
            return from_big_endian<uint32_t>(message_size);
        }
        uint32_t ts() const noexcept
        {
            return from_big_endian<uint32_t>(timestamp);
        }
        auto bytes() const noexcept
        {
            return std::bit_cast<std::array<uint8_t, sizeof(Type01Header)>>(*this);
        }
    };
    struct Type2Header
    {
        BasicHeader basic_header{ .header_type = ChunkType::Type2 };
        std::array<uint8_t, 3> timestamp = { 0, 0, 0 };
        uint32_t ts() const noexcept
        {
            return from_big_endian<uint32_t>(timestamp);
        }
        auto bytes() const noexcept
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
        std::vector<uint8_t> bytes() const noexcept
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
        std::vector<uint8_t> bytes() const noexcept
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
        void parse_amf0(const std::vector<uint8_t>& buffer)
        {
            amf::Message m(buffer);
            bool notify_result = false;
            while (auto value = m.read())
            {
                if (std::holds_alternative<std::string>(*value))
                {
                    auto name = std::get<std::string>(*value);
                    if (name == "_result" || name == "onBWDone" || name == "onStatus")
                        notify_result = true;
                }
                std::visit([](auto&& val) {
                    using T = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<T, std::string>) std::cout << "String: " << val << "\n";
                    else if constexpr (std::is_same_v<T, double>) std::cout << "Number: " << val << "\n";
                    else if constexpr (std::is_same_v<T, bool>) std::cout << "Bool: " << val << "\n";
                    else std::cout << "Null\n";
                }, *value);
            }
            if (notify_result)
                result_cv.notify_all();
        }
        void dump(auto& data) const noexcept
        {
            std::print("[DUMP] ");
            for (uint8_t b : std::bit_cast<std::array<uint8_t, sizeof(data)>>(data))
            {
                std::print("{:X}", b);
                if (std::isprint(b))
                    std::print("'{}' ", (char)b);
                else
                    std::print(" ");
            }
            std::println();
        }
        void dump_data(std::span<uint8_t> data) const noexcept
        {
            std::print("[DUMP {} bytes] ", data.size());
            for (uint8_t b : data)
            {
                std::print("{:X}", b);
                if (std::isprint(b))
                    std::print("'{}' ", (char)b);
                else
                    std::print(" ");
            }
            std::println();
        }
        void parse_chunk_size(std::span<uint8_t> data)
        {
            std::print("SetChunkSize: ");
            if (data.size() != 4)
            {
                std::println("wrong data size");
            }
            else
            {
                uint32_t size = from_big_endian<uint32_t>(data);
                std::println("{}", size);
                packet_max_size = size;
            }
        }
        void parse_window_ack(std::span<uint8_t> data)
        {
            std::print("WindowAckSize: ");
            if (data.size() != 4)
            {
                std::println("wrong data size");
            }
            else
            {
                uint32_t size = from_big_endian<uint32_t>(data);
                std::println("{}", size);
            }
        }
        void parse_client_bw(std::span<uint8_t> data)
        {
            std::print("SetClientBW: ");
            if (data.size() != 5)
            {
                std::println("wrong data size");
            }
            else
            {
                uint32_t size = from_big_endian<uint32_t>(data.subspan(0, 4));
                uint8_t type = data[4];
                std::println("{} type {}", size, type);
            }
        }
        void receive_loop()
        {
            while (true)
            {
                //std::print("basic header: ");
                BasicHeader basic_header;
                int bytes = recv(sockfd, reinterpret_cast<char*>(&basic_header), sizeof(uint8_t), MSG_WAITALL);
                dump(basic_header);
                if (bytes != sizeof(basic_header))
                {
                    std::print("error: unexpected message size of {} bytes\n", bytes);
                    break;
                }
                std::print("[RECV] basic header 0x{:0x} type {}, cs-id {}\n", std::bit_cast<uint8_t>(basic_header),
                           static_cast<uint8_t>(basic_header.header_type), static_cast<uint8_t>(basic_header.stream_id));
                if (basic_header.header_type == ChunkType::Type0)
                {
                    FullHeader fh { .basic_header = basic_header };
                    bytes = recv(sockfd, reinterpret_cast<char*>(&fh) + sizeof(BasicHeader),
                                 sizeof(FullHeader) - sizeof(BasicHeader), MSG_WAITALL);
                    dump(fh);
                    if (bytes != sizeof(FullHeader) - sizeof(BasicHeader))
                    {
                        std::print("error: unexpected message size of {} bytes\n", bytes);
                        break;
                    }
                    auto message_size = fh.size();
                    std::print(" - full header: message type {}, size {} bytes, ts 0x{:X}\n", to_string(fh.message_type), message_size, fh.ts());

                    int32_t request_size = std::min<int32_t>(message_size, packet_max_size);

                    auto message_buffer = std::vector<uint8_t>(request_size, 0);
                    bytes = recv(sockfd, reinterpret_cast<char*>(message_buffer.data()), (int)message_buffer.size(), MSG_WAITALL);
                    dump_data(message_buffer);
                    if (bytes != message_buffer.size())
                    {
                        std::print("error: unexpected message size of {} bytes\n", bytes);
                        break;
                    }

                    if (message_size > packet_max_size)
                    {
                        stream_buffer[basic_header.stream_id].append_range(message_buffer);
                    }
                    else
                    {
                        switch (fh.message_type)
                        {
                            case MessageType::SetChunkSize:
                                parse_chunk_size(message_buffer);
                                break;
                            case MessageType::CommandAMF0:
                                parse_amf0(message_buffer);
                                break;
                            case MessageType::WindowAcknowledgementSize:
                                parse_window_ack(message_buffer);
                                break;
                            case MessageType::SetPeerBandwidth:
                                parse_client_bw(message_buffer);
                                break;
                        }
                    }

                    last_header[basic_header.stream_id] = fh;
                }
                else if (basic_header.header_type == ChunkType::Type1)
                {
                    Type01Header h1{ .basic_header = basic_header };
                    bytes = recv(sockfd, reinterpret_cast<char*>(&h1) + sizeof(BasicHeader),
                                 sizeof(Type01Header) - sizeof(BasicHeader), MSG_WAITALL);
                    dump(h1);
                    if (bytes != sizeof(Type01Header) - sizeof(BasicHeader))
                    {
                        std::print("error: unexpected message size of {} bytes\n", bytes);
                        break;
                    }

                    FullHeader& fh = last_header[basic_header.stream_id];
                    fh.timestamp = h1.timestamp;
                    fh.message_size = h1.message_size;
                    fh.message_type = h1.message_type;

                    auto message_size = fh.size();
                    std::print(" - type01 header: message type {}, size {} bytes, timestamp {:X}\n", to_string(fh.message_type), message_size, fh.ts());

                    int32_t request_size = std::min<int32_t>(message_size, packet_max_size);
                    auto message_buffer = std::vector<uint8_t>(request_size, 0);
                    bytes = recv(sockfd, reinterpret_cast<char*>(message_buffer.data()), (int)message_buffer.size(), MSG_WAITALL);
                    if (bytes != message_buffer.size())
                    {
                        std::print("error: unexpected message size of {} bytes\n", bytes);
                        break;
                    }

                    if (message_size > packet_max_size)
                    {
                        stream_buffer[basic_header.stream_id].append_range(message_buffer);
                    }
                    else
                    {
                        switch (fh.message_type)
                        {
                            case MessageType::SetChunkSize:
                                parse_chunk_size(message_buffer);
                                break;
                            case MessageType::CommandAMF0:
                                parse_amf0(message_buffer);
                                break;
                            case MessageType::WindowAcknowledgementSize:
                                parse_window_ack(message_buffer);
                                break;
                            case MessageType::SetPeerBandwidth:
                                parse_client_bw(message_buffer);
                                break;
                        }
                    }
                }
                else if (basic_header.header_type == ChunkType::Type2)
                {
                    Type2Header h1{ .basic_header = basic_header };
                    bytes = recv(sockfd, reinterpret_cast<char*>(&h1) + sizeof(BasicHeader),
                                 sizeof(Type2Header) - sizeof(BasicHeader), MSG_WAITALL);
                    dump(h1);
                    if (bytes != sizeof(Type2Header) - sizeof(BasicHeader))
                    {
                        std::print("error: unexpected message size of {} bytes\n", bytes);
                        break;
                    }

                    FullHeader& fh = last_header[basic_header.stream_id];
                    fh.timestamp = h1.timestamp;

                    auto message_size = fh.size();
                    std::print(" - type01 header: message type {}, size {} bytes, timestamp {:X}\n", to_string(fh.message_type), message_size, fh.ts());

                    int32_t request_size = std::min<int32_t>(message_size, packet_max_size);
                    auto message_buffer = std::vector<uint8_t>(request_size, 0);
                    bytes = recv(sockfd, reinterpret_cast<char*>(message_buffer.data()), (int)message_buffer.size(), MSG_WAITALL);
                    if (bytes != message_buffer.size())
                    {
                        std::print("error: unexpected message size of {} bytes\n", bytes);
                        break;
                    }

                    if (message_size > packet_max_size)
                    {
                        stream_buffer[basic_header.stream_id].append_range(message_buffer);
                    }
                    else
                    {
                        switch (fh.message_type)
                        {
                            case MessageType::SetChunkSize:
                                parse_chunk_size(message_buffer);
                                break;
                            case MessageType::CommandAMF0:
                                parse_amf0(message_buffer);
                                break;
                            case MessageType::WindowAcknowledgementSize:
                                parse_window_ack(message_buffer);
                                break;
                            case MessageType::SetPeerBandwidth:
                                parse_client_bw(message_buffer);
                                break;
                        }
                    }
                }
                else if (basic_header.header_type == ChunkType::Type3)
                {
                    FullHeader fh = last_header[basic_header.stream_id];
                    auto message_size = fh.size();

                    uint32_t remaining_size = message_size - (uint32_t)stream_buffer[basic_header.stream_id].size();
                    uint32_t request_size = std::min<int32_t>(remaining_size, packet_max_size);

                    auto message_buffer = std::vector<uint8_t>(request_size, 0);
                    bytes = recv(sockfd, reinterpret_cast<char*>(message_buffer.data()), (int)message_buffer.size(), MSG_WAITALL);
                    if (bytes != message_buffer.size())
                    {
                        std::print("error: unexpected message size of {} bytes\n", bytes);
                        break;
                    }
                    std::println(" - type3 header: continuation of header type {} with message type {}, size {}",
                                 static_cast<uint8_t>(fh.basic_header.header_type), to_string(fh.message_type), message_size);

                    if (remaining_size > packet_max_size)
                    {
                        stream_buffer[basic_header.stream_id].append_range(message_buffer);
                    }
                    else
                    {
                        stream_buffer[basic_header.stream_id].append_range(message_buffer);
                        switch (fh.message_type)
                        {
                            case MessageType::SetChunkSize:
                                parse_chunk_size(stream_buffer[basic_header.stream_id]);
                                break;
                            case MessageType::CommandAMF0:
                                parse_amf0(stream_buffer[basic_header.stream_id]);
                                break;
                            case MessageType::WindowAcknowledgementSize:
                                parse_window_ack(stream_buffer[basic_header.stream_id]);
                                break;
                            case MessageType::SetPeerBandwidth:
                                parse_client_bw(stream_buffer[basic_header.stream_id]);
                                break;
                        }
                        stream_buffer.erase(basic_header.stream_id);
                    }
                }
                else
                {
                    std::println(" - type {:x} header not supported", static_cast<uint8_t>(basic_header.header_type));
                }
            }
        }
    public:
        bool connect_host(std::string_view host, int32_t port) noexcept
        {
#if defined(_WIN32)
            WSADATA wsaData{0};
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
                return false;
#endif
            addrinfo hints{}, * result = nullptr;
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;

            if (getaddrinfo(host.data(), std::to_string(port).c_str(), &hints, &result) != 0)
            {
                std::cerr << "getaddrinfo failed\n";
#if defined(_WIN32)
                WSACleanup();
#endif
                return false;
            }

            sockfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
            if (connect(sockfd, result->ai_addr, (int)result->ai_addrlen) < 0)
            {
#if defined(_WIN32)
                closesocket(sockfd);
                WSACleanup();
#else
                ::close(sockfd);
#endif
                return false;
            }

            m_host = host;
            return true;
        }
        void close()
        {
#if defined(_WIN32)
            closesocket(sockfd);
            WSACleanup();
#else
            ::close(sockfd);
#endif
            if (receive_thread.joinable()) {
                receive_thread.join();
            }
        }
        bool handshake() noexcept
        {
            std::vector<uint8_t> c0c1(1537);
            c0c1[0] = 0x03;
            for (int i = 1; i < 1537; ++i) c0c1[i] = rand() % 256;
            send(sockfd, (const char*)c0c1.data(), 1537, 0);

            uint8_t s0s1s2[3073];
            recv(sockfd, (char*)s0s1s2, 3073, MSG_WAITALL);
            send(sockfd, (const char*)(s0s1s2 + 1), 1536, 0);
            return true;
        }
        void wait_result()
        {
            std::unique_lock lock(mutex);
            result_cv.wait(lock);
        }
        void start_receiving()
        {
            receive_thread = std::thread(&Socket::receive_loop, this);
        }
        template<typename T>
        bool send_packet(const T& packet) const noexcept
        {
            auto data = packet.bytes();
            send(sockfd, (const char*)data.data(), (int)data.size(), 0);
            std::println("[SEND] command: {} bytes", data.size());
            return true;
        }
        template<typename T>
        bool send_packet_with_data(const T& packet, std::span<uint8_t> raw_data) const noexcept
        {
            auto data = packet.bytes();
            data.append_range(raw_data);
            send(sockfd, (const char*)data.data(), data.size(), 0);
            std::println("[SEND] command: {} bytes", data.size());
            return true;
        }
        bool send_packets(const std::vector<Packet>& packets) const noexcept
        {
            std::vector<uint8_t> data;
            for (const auto& p : packets)
                data.append_range(p.bytes());
            send(sockfd, (const char*)data.data(), (int)data.size(), 0);
            std::println("[SEND] multiple commands: {} bytes", data.size());
            return true;
        }
        void send_chunk_size()
        {
            FullHeader header;
            header.basic_header.stream_id = 2;
            header.message_type = MessageType::SetChunkSize;
            header.message_size = trunc_be<3>(to_big_endian(static_cast<uint32_t>(4)));
            std::vector<uint8_t> packet;
            packet.append_range(header.bytes());
            packet.append_range(std::array<uint8_t, 4>({ 0x7F, 0xFF, 0xFF, 0xFF, }));
            send(sockfd, (const char*)packet.data(), (int)packet.size(), 0);
        }
        void send_connect_command(std::string_view app)
        {
            rtmp::Packet packet;
            packet.body.write_string("connect");
            packet.body.write_number(1.0);
            packet.body.write_object({
                                             { "app", std::string{app} },
                                             { "type", "nonprivate" },
                                             { "tcUrl", std::format( "rtmp://{}/{}", m_host, app ) },
                                             //{ "swfUrl", "rtmp://a.rtmp.youtube.com/live2" },
                                             //{ "flashVer", "FMLE/3.0" },
                                     });
            packet.update_header();
            std::println("[SEND] commands: connect");
            send_packet(packet);
            std::println("[WAIT] _result");
            wait_result();
        }
        void send_create_stream_command(std::string_view key)
        {
            rtmp::Packet01 release_stream;
            release_stream.header.basic_header.stream_id = 3;
            release_stream.body.write_string("releaseStream");
            release_stream.body.write_number(2.0);
            release_stream.body.write_null();
            release_stream.body.write_string(key);
            release_stream.update_header();
            send_packet(release_stream);

            rtmp::Packet01 fcpublish_stream;
            fcpublish_stream.header.basic_header.stream_id = 3;
            fcpublish_stream.body.write_string("FCPublish");
            fcpublish_stream.body.write_number(3.0);
            fcpublish_stream.body.write_null();
            fcpublish_stream.body.write_string(key);
            fcpublish_stream.update_header();
            send_packet(fcpublish_stream);

            rtmp::Packet create_stream;
            create_stream.header.basic_header.stream_id = 3;
            create_stream.body.write_string("createStream");
            create_stream.body.write_number(4.0);
            create_stream.body.write_null();
            create_stream.update_header();
            send_packet(create_stream);

            //std::vector<uint8_t> data;
            //data.append_range(release_stream.bytes());
            //data.append_range(fcpublish_stream.bytes());
            //data.append_range(create_stream.bytes());

            //std::println("[SEND] commands: releaseStream, FCPublish, createStream");
            //send(sockfd, (const char*)data.data(), data.size(), 0);
            std::println("[WAIT] _result");
            wait_result();
            std::println("[WAIT] onBWDone");
            //wait_result();

            rtmp::Packet checkbw;
            checkbw.header.basic_header.stream_id = 3;
            checkbw.body.write_string("_checkbw");
            checkbw.body.write_number(5.0);
            checkbw.body.write_null();
            checkbw.update_header();
            send_packet(checkbw);
        }
        void send_publish_command(std::string_view key)
        {
            rtmp::Packet packet;
            packet.header.basic_header.stream_id = 4;
            packet.header.message_streamd_id = 1;
            packet.body.write_string("publish");
            packet.body.write_number(6.0);
            packet.body.write_null();
            packet.body.write_string(key);
            packet.body.write_string("live");
            packet.update_header();
            send_packet(packet);
            wait_result();
        }
        void send_close_command(std::string_view key)
        {
            rtmp::Packet01 unpublish;
            unpublish.header.basic_header.stream_id = 3;
            unpublish.body.write_string("FCUnpublish");
            unpublish.body.write_number(7.0);
            unpublish.body.write_null();
            unpublish.body.write_string(key);
            unpublish.update_header();
            send_packet(unpublish);

            rtmp::Packet delete_stream;
            delete_stream.header.basic_header.stream_id = 3;
            delete_stream.body.write_string("deleteStream");
            delete_stream.body.write_number(8.0);
            delete_stream.body.write_null();
            delete_stream.body.write_number(1.0);
            delete_stream.update_header();
            send_packet(delete_stream);
        }
        void send_audio_header(std::span<const uint8_t> aac_config)
        {
            std::vector<uint8_t> payload;
            payload.push_back(0xAF); // AAC + raw
            payload.push_back(0x00); // AudioSpecificConfig
            payload.append_range(aac_config);
            // setup rmpt packet
            rtmp::FullHeader header;
            header.basic_header.stream_id = 4;
            header.message_type = MessageType::AudioPacket;
            header.message_streamd_id = 1;
            header.message_size = trunc_be<3>(to_big_endian(static_cast<uint32_t>(payload.size())));
            std::vector<uint8_t> packet;
            packet.append_range(header.bytes());
            packet.append_range(payload);
            send(sockfd, (const char*)packet.data(), (int)packet.size(), 0);
        }
        void send_audio_aac(std::span<const uint8_t> aac_raw, uint32_t ts)
        {
            std::vector<uint8_t> payload;
            payload.push_back(0xAF); // AAC + raw
            payload.push_back(0x01); // raw AAC frame
            payload.append_range(aac_raw);
            // setup rmpt packet
            rtmp::FullHeader header;
            header.basic_header.stream_id = 4;
            header.message_type = MessageType::AudioPacket;
            header.message_streamd_id = 1;
            header.message_size = trunc_be<3>(to_big_endian(static_cast<uint32_t>(payload.size())));
            header.timestamp = trunc_be<3>(to_big_endian(static_cast<uint32_t>(ts)));
            std::vector<uint8_t> packet;
            packet.append_range(header.bytes());
            packet.append_range(payload);
            int sent = send(sockfd, (const char*)packet.data(), (int)packet.size(), 0);
            if (sent != packet.size())
                std::println("send_audio_aac FAILED");
        }
        void send_video_header(std::span<const uint8_t> sps, std::span<const uint8_t> pps)
        {
            std::vector<uint8_t> buf;
            buf.push_back(0x17);         // keyframe + AVC
            buf.push_back(0x00);         // AVC sequence header
            buf.push_back(0x00); buf.push_back(0x00); buf.push_back(0x00); // composition time
            buf.push_back(0x01);         // AVCDecoderConfigurationRecord version
            buf.push_back(sps[1]);       // profile
            buf.push_back(sps[2]);       // compatibility
            buf.push_back(sps[3]);       // level
            buf.push_back(0xFF);         // 4 bytes NALU length
            // SPS
            buf.push_back(0xE1);         // 1 SPS
            buf.push_back((sps.size() >> 8) & 0xFF);
            buf.push_back(sps.size() & 0xFF);
            buf.append_range(sps);
            // PPS
            buf.push_back(0x01);         // 1 PPS
            buf.push_back((pps.size() >> 8) & 0xFF);
            buf.push_back(pps.size() & 0xFF);
            buf.append_range(pps);
            // setup rmpt packet
            rtmp::FullHeader header;
            header.basic_header.stream_id = 6;
            header.message_type = MessageType::VideoPacket;
            header.message_streamd_id = 1;
            header.message_size = trunc_be<3>(to_big_endian(static_cast<uint32_t>(buf.size())));
            std::vector<uint8_t> packet;
            packet.append_range(header.bytes());
            packet.append_range(buf);
            int sent = send(sockfd, (const char*)packet.data(), (int)packet.size(), 0);
            if (sent != packet.size())
                std::println("send_video_header FAILED");
        }
        void send_video_h264(std::vector<std::span<const uint8_t>> nals, uint32_t ts, bool keyframe)
        {
            std::vector<uint8_t> buf;
            buf.push_back(keyframe ? 0x17 : 0x27); // keyframe/interframe + AVC
            buf.push_back(0x01); // AVC NALU
            buf.push_back(0x00); buf.push_back(0x00); buf.push_back(0x00); // composition time
            for (auto& n : nals)
            {
                buf.append_range(to_big_endian<uint32_t>((int)n.size()));
                buf.append_range(n);
            }
            // setup rmpt packet
            rtmp::FullHeader header;
            header.basic_header.stream_id = 6;
            header.message_type = MessageType::VideoPacket;
            header.message_streamd_id = 1;
            header.message_size = trunc_be<3>(to_big_endian(static_cast<uint32_t>(buf.size())));
            header.timestamp = trunc_be<3>(to_big_endian(static_cast<uint32_t>(ts)));
            std::vector<uint8_t> packet;
            packet.append_range(header.bytes());
            packet.append_range(buf);
            int sent = send(sockfd, (const char*)packet.data(), (int)packet.size(), 0);
            if (sent != packet.size())
                std::println("send_video_h264 FAILED");
        }
        void parse_rtmp_message(const uint8_t* buf, size_t len)
        {
            std::cout << "[RECV] " << len << " bytes\n";
            for (size_t i = 0; i < len; ++i)
                printf("%02X ", buf[i]);
            std::cout << "\n";

            if (len > 12 && buf[0] == 0x02) {
                //parse_amf(buf + 12, len - 12);
            }
        }
        void wait_response()
        {
            uint8_t buf[4096];
            int r = recv(sockfd, (char*)buf, sizeof(buf), 0);
            if (r > 0) {
                parse_rtmp_message(buf, r);
            }
            else {
                std::cerr << "No data received or connection error.\n";
            }
        }
    };
}
