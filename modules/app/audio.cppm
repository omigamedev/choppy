module;
#include <cstdio>
#include <string>
#include <memory>
#include <variant>
#include <unordered_map>
#include <miniaudio.h>
#include <extras/decoders/libopus/miniaudio_libopus.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#else
#define LOGE(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

export module ce.app:audio;
import :utils;
import :globals;

export namespace ce::app::audio
{
struct my_sound
{
    ma_decoder decoder{};
    ma_libopus opus_source{};
    ma_sound sound{};
    std::vector<uint8_t> data{};
    ma_uint64 cursor = 0;

    static ma_result read(void* pUserData, void* pBuffer, const size_t bytesToRead, size_t* pBytesRead)
    {
        my_sound* context = static_cast<my_sound*>(pUserData);
        const size_t bytesAvailable = context->data.size() - context->cursor;
        const size_t bytesActuallyRead = std::min(bytesToRead, bytesAvailable);
        memcpy(pBuffer, context->data.data() + context->cursor, bytesActuallyRead);
        context->cursor += bytesActuallyRead;
        *pBytesRead = bytesActuallyRead;
        return MA_SUCCESS;
    }
    static ma_result seek(void* pUserData, const ma_int64 offset, const ma_seek_origin origin)
    {
        my_sound* context = static_cast<my_sound*>(pUserData);
        ma_uint64 newCursor = context->cursor;

        if (origin == ma_seek_origin_start)
        {
            newCursor = offset;
        }
        else if (origin == ma_seek_origin_current)
        {
            newCursor = context->cursor + offset;
        }
        else if (origin == ma_seek_origin_end)
        {
            newCursor = context->data.size() + offset; // Note: offset should be negative from end
        }

        // Check bounds
        if (newCursor < 0 || newCursor > context->data.size())
        {
            return MA_BAD_SEEK; // Failed to seek
        }

        context->cursor = newCursor;
        return MA_SUCCESS;
    }
    static ma_result tell(void* pUserData, ma_int64* pCursor)
    {
        my_sound* context = static_cast<my_sound*>(pUserData);
        *pCursor = context->cursor;
        return MA_SUCCESS;
    }
};
class AudioSystem : public utils::NoCopy
{
    std::unordered_map<std::string, std::shared_ptr<my_sound>> data_sources;
public:
    bool create_system() noexcept
    {
        if (ma_engine_init(nullptr, &globals::audio_engine) != MA_SUCCESS)
        {
            LOGE("failed to init miniaudio");
            return false;
        }
        return true;
    }
    void destroy_system() noexcept
    {
        ma_engine_uninit(&globals::audio_engine);
    }
    void play_sound(const std::string& name) noexcept
    {
        const std::string path = "assets/audio/" + name;
        auto& new_sound = data_sources[name];
        if (new_sound)
        {
            ma_sound_start(&new_sound->sound);
            return;
        }

        new_sound = std::make_shared<my_sound>();
        auto data = platform::GetPlatform().read_file(path);
        if (!data)
        {
            LOGE("failed to read audio file %s", path.c_str());
        }

        new_sound->data = std::move(*data);

        ma_result result{};

        // ma_decoder_config decoder_config = ma_decoder_config_init(ma_format_unknown, 0, 0);
        // decoder_config.encodingFormat = ma_encoding_format_wav;
        // result = ma_decoder_init_memory(
        //     new_sound->data.data(), // Pointer to the raw data
        //     new_sound->data.size(), // Size of the data in bytes
        //     &decoder_config,         // Configuration (optional, NULL for defaults)
        //     &new_sound->decoder                 // Output decoder object
        // );
        result = ma_libopus_init(
                my_sound::read,         // The custom read function
                my_sound::seek,         // The custom seek function
                my_sound::tell,         // The custom tell function
                new_sound.get(),        // The pointer to your persistent data struct
                NULL,                   // Config (using NULL for default ma_decoding_backend_config)
                NULL,                   // Allocation Callbacks (using NULL for default)
                &new_sound->opus_source // Output ma_libopus structure
        );
        if (result != MA_SUCCESS)
        {
            LOGE("Failed to initialize decoder from memory.");
            return;
        }

        const ma_sound_config sound_config = ma_sound_config_init();
        result = ma_sound_init_from_data_source(
            &globals::audio_engine,
            &new_sound->opus_source,    // The data source (your memory decoder)
            0,                      // Flags (e.g., MA_SOUND_FLAG_LOOP)
            nullptr,                // Group (optional)
            &new_sound->sound       // Output sound object
        );
        if (result != MA_SUCCESS)
        {
            LOGE("Failed to initialize sound from decoder.");
            // Handle error
            ma_decoder_uninit(&new_sound->decoder);
        }

        ma_sound_start(&new_sound->sound);
    }
    void tick(const float dt) noexcept
    {

    }
};
}