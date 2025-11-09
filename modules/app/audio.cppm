module;
#include <cstdio>
#include <string>
#include <miniaudio.h>

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
class AudioSystem : public utils::NoCopy
{
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
        if (ma_engine_play_sound(&globals::audio_engine, path.c_str(), nullptr) != MA_SUCCESS)
        {
            LOGE("failed to play sound");
        }
    }
    void tick(const float dt) noexcept
    {

    }
};
}