#pragma once
#include <span>
#include <vector>
#include <cstdint>
#include <functional>

struct AMediaCodec;
struct AMediaFormat;

class VideoEncoder
{
    AMediaCodec* codec = nullptr;
    AMediaFormat* format = nullptr;
    int width = 1280;
    int height = 720;
    int bitrate = 10 << 20;
    int fps = 30;
    int iframeInterval = 1; // seconds
    std::vector<std::span<const uint8_t>> extract_nals_annexb(std::span<const uint8_t> d);
    std::function<void(std::span<const uint8_t> sps, std::span<const uint8_t> pps)> config_handler;
    std::function<void(std::vector<std::span<const uint8_t>> nals, uint64_t pts_ms, bool keyframe)> packet_handler;
public:
    VideoEncoder(int width, int height, int bitrate, int fps) noexcept;
    bool create() noexcept;
    bool send_frame(std::span<const uint8_t> rgba, uint64_t ts_ms) noexcept;
    bool receive_packet() noexcept;
    void on_config(std::function<void(std::span<const uint8_t> sps, std::span<const uint8_t> pps)>&& handler) noexcept { config_handler = handler; }
    void on_packet(std::function<void(std::vector<std::span<const uint8_t>> nals, uint64_t pts_ms, bool keyframe)>&& handler) noexcept { packet_handler = handler; }
};
