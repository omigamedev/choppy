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
    int32_t max_input_bytes = 0;
    int32_t pcm_encoding = 0;
    std::function<void(std::span<const uint8_t> csd)> config_handler;
    std::function<void(std::span<const uint8_t> pkt, uint64_t pts_ms)> packet_handler;
public:
    AudioEncoder(int samplerate, int channels, int bitrate) noexcept;
    bool create() noexcept;
    bool send_frame(std::span<const int16_t> pcm, uint64_t ts_ms) noexcept;
    bool receive_packet() noexcept;
    void on_config(std::function<void(std::span<const uint8_t> csd)>&& handler) noexcept { config_handler = handler; }
    void on_packet(std::function<void(std::span<const uint8_t> pkt, uint64_t pts_ms)>&& handler) noexcept { packet_handler = handler; }
    // max number of samples the input buffer can fit, regardless of the number of channels
    [[nodiscard]] int32_t max_input_samples() const noexcept { return max_input_bytes / sizeof(int16_t); }
    int32_t get_samplerate() const noexcept { return samplerate; }
};
