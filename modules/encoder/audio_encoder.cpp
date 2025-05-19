#include "audio_encoder.h"
#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyAudioEncoder", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyAudioEncoder", __VA_ARGS__)

AudioEncoder::AudioEncoder(int samplerate, int channels, int bitrate) noexcept
    : samplerate(samplerate), channels(channels), bitrate(bitrate)
{

}

bool AudioEncoder::create() noexcept
{
    format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm"); // H.264
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, bitrate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, samplerate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, channels);

    codec = AMediaCodec_createEncoderByType("audio/mp4a-latm");
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

bool AudioEncoder::send_frame(std::span<const uint8_t> rgba, uint64_t ts_ms) noexcept
{
    return false;
}

bool AudioEncoder::receive_packet(std::vector<uint8_t> &out, uint64_t &out_pts) noexcept
{
    return false;
}

void AudioEncoder::flush() noexcept
{

}

void AudioEncoder::on_config(
    std::function<void(std::vector<uint8_t>, std::vector<uint8_t>)> handler) noexcept
{

}
