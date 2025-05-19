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
    uint64_t ptsUs = 0; // Presentation timestamp in microseconds
    std::vector<std::span<const uint8_t>> extract_nals_annexb(std::span<const uint8_t> d);
    std::vector<uint8_t> sps, pps;
    std::function<void(std::vector<uint8_t> sps, std::vector<uint8_t> pps)> config_handler;
public:
    VideoEncoder(int width, int height, int bitrate, int fps) noexcept;
    bool create() noexcept;
    bool send_frame(std::span<const uint8_t> rgba, uint64_t ts_ms) noexcept;
    bool receive_packet(std::vector<uint8_t>& out, uint64_t& out_pts) noexcept;
    void flush() noexcept;
    void on_config(std::function<void(std::vector<uint8_t> sps, std::vector<uint8_t> pps)> handler) noexcept;
};
