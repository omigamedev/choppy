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
    ma_sound sound{};
    std::vector<uint8_t> data{};
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
        const auto data = platform::GetPlatform().read_file(path);
        if (!data)
        {
            LOGE("failed to read audio file %s", path.c_str());
        }

        new_sound->data = std::move(*data);
        ma_decoder_config decoder_config = ma_decoder_config_init(ma_format_unknown, 0, 0);
        decoder_config.encodingFormat = ma_encoding_format_wav;

        ma_result result{};
        result = ma_decoder_init_memory(
            new_sound->data.data(), // Pointer to the raw data
            new_sound->data.size(), // Size of the data in bytes
            &decoder_config,         // Configuration (optional, NULL for defaults)
            &new_sound->decoder                 // Output decoder object
        );

        if (result != MA_SUCCESS)
        {
            LOGE("Failed to initialize decoder from memory.");
            // Handle error
            return;
        }

        const ma_sound_config sound_config = ma_sound_config_init();
        // Initialize the sound from the decoder (which acts as a ma_data_source)
        result = ma_sound_init_from_data_source(
            &globals::audio_engine,
            &new_sound->decoder,    // The data source (your memory decoder)
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
        // if (ma_engine_play_sound(&globals::audio_engine, path.c_str(), nullptr) != MA_SUCCESS)
        // {
        //     LOGE("failed to play sound");
        // }
    }
    void tick(const float dt) noexcept
    {

    }
};
}