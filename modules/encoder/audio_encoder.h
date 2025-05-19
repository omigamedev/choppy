#pragma once

#include <span>
#include <vector>
#include <cstdint>
#include <functional>

struct AMediaCodec;
struct AMediaFormat;

class AudioEncoder
{
    AMediaCodec* codec = nullptr;
    AMediaFormat* format = nullptr;
    int samplerate = 48000;
    int channels = 2;
    int bitrate = 10 << 20;
    uint64_t ptsUs = 0; // Presentation timestamp in microseconds
    std::function<void(std::vector<uint8_t> sps, std::vector<uint8_t> pps)> config_handler;
public:
    AudioEncoder(int samplerate, int channels, int bitrate) noexcept;
    bool create() noexcept;
    bool send_frame(std::span<const uint8_t> rgba, uint64_t ts_ms) noexcept;
    bool receive_packet(std::vector<uint8_t>& out, uint64_t& out_pts) noexcept;
    void flush() noexcept;
    void on_config(std::function<void(std::vector<uint8_t> sps, std::vector<uint8_t> pps)> handler) noexcept;
};
