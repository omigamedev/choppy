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
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_PCM_ENCODING, 2);

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

    AMediaFormat* input_format = AMediaCodec_getInputFormat(codec);
    AMediaFormat_getInt32(input_format, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, &max_input_bytes);
    AMediaFormat_getInt32(input_format, AMEDIAFORMAT_KEY_PCM_ENCODING, &pcm_encoding);
    AMediaFormat_delete(input_format);

    AMediaCodec_start(codec);
    return true;
}

bool AudioEncoder::send_frame(std::span<const int16_t> pcm, uint64_t ts_ms) noexcept
{
    ssize_t bufIndex = AMediaCodec_dequeueInputBuffer(codec, -1);
    if (bufIndex >= 0)
    {
        size_t bufSize = 0;
        uint8_t* inputBuf = AMediaCodec_getInputBuffer(codec, bufIndex, &bufSize);
        std::ranges::copy(pcm, reinterpret_cast<int16_t*>(inputBuf));
        AMediaCodec_queueInputBuffer(codec, bufIndex, 0, pcm.size_bytes(), ts_ms * 1000, 0);
        LOGI("send audio frame ts %lu", ts_ms);
        return true;
    }
    return false;
}

bool AudioEncoder::receive_packet() noexcept
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

        if (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)
        {
            LOGI("Got codec config");
            if (config_handler)
                config_handler({ outBuf + info.offset, (uint32_t)info.size });
        }
        else if (info.size > 0)
        {
            LOGI("Encoded audio frame %d bytes, pts %ld", info.size, info.presentationTimeUs / 1000);
            if (packet_handler)
                packet_handler({ outBuf + info.offset, (uint32_t)info.size }, info.presentationTimeUs / 1000);
            AMediaCodec_releaseOutputBuffer(codec, outputBufIndex, false);
            return true;
        }

        AMediaCodec_releaseOutputBuffer(codec, outputBufIndex, false);
    }
    return false;
}
