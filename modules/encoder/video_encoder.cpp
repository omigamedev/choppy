#include "video_encoder.h"
#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <libyuv.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyVideoEncoder", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyVideoEncoder", __VA_ARGS__)

#define AMEDIAFORMAT_COLOR_FormatYUV420Flexible 0x7F420888

bool VideoEncoder::create() noexcept
{
    format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc"); // H.264
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, width);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, height);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, bitrate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, fps);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, iframeInterval);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, AMEDIAFORMAT_COLOR_FormatYUV420Flexible); // Can vary based on source

    codec = AMediaCodec_createEncoderByType("video/avc");
    if (!codec)
    {
        LOGE("Failed to create encoder");
        return false;
    }

    media_status_t status = AMediaCodec_configure(codec, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    if (status != AMEDIA_OK)
    {
        LOGE("Failed to configure codec: %d", status);
        return false;
    }

    AMediaCodec_start(codec);
    return true;
}

bool VideoEncoder::send_frame(std::span<const uint8_t> rgba, uint64_t ts_ms) noexcept
{
    ssize_t bufIndex = AMediaCodec_dequeueInputBuffer(codec, -1);
    if (bufIndex >= 0)
    {
        size_t bufSize;
        uint8_t* inputBuf = AMediaCodec_getInputBuffer(codec, bufIndex, &bufSize);
        const int ySize = width * height;
        const int uvSize = (width / 2) * (height / 2);
        libyuv::ABGRToI420(
            rgba.data(), width * 4,
            inputBuf, width,
            inputBuf + ySize, width / 2,
            inputBuf + ySize + uvSize, width / 2,
            width, height
        );
        AMediaCodec_queueInputBuffer(codec, bufIndex, 0, bufSize, ts_ms * 1000, 0);
        LOGI("send video frame ts %lu", ts_ms);
        return true;
    }
    return false;
}

bool VideoEncoder::receive_packet() noexcept
{
    AMediaCodecBufferInfo info;
    while (true)
    {
        ssize_t outputBufIndex = AMediaCodec_dequeueOutputBuffer(codec, &info, 0);

        if (outputBufIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) { return false; }
        else if (outputBufIndex == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) { return false; }
        else if (outputBufIndex == AMEDIACODEC_INFO_TRY_AGAIN_LATER) { return false; }
        else if (outputBufIndex < 0) { return false; }

        size_t outSize;
        const uint8_t* outBuf = AMediaCodec_getOutputBuffer(codec, outputBufIndex, &outSize);

        const auto nals = extract_nals_annexb({ outBuf + info.offset, outBuf + info.size });
        if (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)
        {
            // SPS/PPS header
            LOGI("Got codec config");
            std::span<const uint8_t> sps, pps;
            for (const auto& nal : nals)
            {
                if (nal.empty()) continue;
                uint8_t nal_unit_type = nal[0] & 0x1F;
                if (nal_unit_type == 0x07) sps = nal;
                if (nal_unit_type == 0x08) pps = nal;
            }
            if (config_handler)
                config_handler(sps, pps);
        }
        else if (info.size > 0)
        {
            LOGI("Encoded video frame %d bytes, pts %ld", info.size, info.presentationTimeUs / 1000);
            if (packet_handler)
                packet_handler(nals, info.presentationTimeUs / 1000, info.flags & AMEDIACODEC_BUFFER_FLAG_KEY_FRAME);
            AMediaCodec_releaseOutputBuffer(codec, outputBufIndex, false);
            return true;
        }

        AMediaCodec_releaseOutputBuffer(codec, outputBufIndex, false);
    }
    return false;
}

VideoEncoder::VideoEncoder(int width, int height, int bitrate, int fps) noexcept
    : width(width), height(height), bitrate(bitrate), fps(fps)
{

}

std::vector<std::span<const uint8_t>> VideoEncoder::extract_nals_annexb(std::span<const uint8_t> d)
{
    std::vector<std::span<const uint8_t>> nal_units;
    off_t start = 4;
    off_t i = 4;
    while (i + 4 < d.size())
    {
        if (d[i] == 0x00 && d[i + 1] == 0x00 && d[i + 2] == 0x00 && d[i + 3] == 0x01)
        {
            nal_units.emplace_back(d.begin() + start, d.begin() + i);
            i += 4;
            start = i;
        }
        else
        {
            i++;
        }
    }
    nal_units.emplace_back(d.begin() + start, d.end());
    return nal_units;
}
