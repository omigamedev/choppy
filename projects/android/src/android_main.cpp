#include <lib.h>

#include <jni.h>
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <video_encoder.h>
#include <audio_encoder.h>
#include <rtmp.h>
#include <memory>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)

import ce.vk;
import ce.xr;
import ce.platform;
import ce.platform_android;

class AndroidContext
{
    std::unique_ptr<ce::platform::PlatformAndroid> platform;
    ce::xr::Instance xr_instance;
    ce::vk::Context vk_context;
    android_app *pApp;
public:
    AndroidContext(android_app *pApp) : pApp(pApp) { }
    bool create()
    {
        platform = std::make_unique<ce::platform::PlatformAndroid>();
        xr_instance.setup_android(pApp->activity->vm, pApp->activity->javaGameActivity);
        xr_instance.create();
        vk_context.create_instance(xr_instance.vulkan_version(), xr_instance.instance_extensions());
        auto xr_physical_device = xr_instance.physical_device(vk_context.instance());
        vk_context.create_device(xr_physical_device, xr_instance.device_extensions());
        auto supported_formats = xr_instance.enumerate_swapchain_formats();
        // vk: pick a format for color and depth
        // vk: create the swapchain renderpass
        // xr: enumerate views
        // for each view
        //     vk: create the swapchain image views
        //
        return true;
    }
    bool start_session()
    {
        xr_instance.start_session(vk_context.device(),
            vk_context.queue_family_index(), vk_context.queue_index());
        return true;
    }
    void main_loop()
    {
        int events;
        android_poll_source *pSource;
        do
        {
            // Process all pending events before running game logic.
            if (ALooper_pollOnce(-1, nullptr, &events, (void **) &pSource) >= 0)
            {
                if (pSource)
                {
                    pSource->process(pApp, pSource);
                }
            }
        } while(!pApp->destroyRequested);
    }
};

void handle_cmd(android_app *pApp, int32_t cmd)
{
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (auto context = reinterpret_cast<AndroidContext*>(pApp->userData))
            {
                context->start_session();
            }
            break;
        case APP_CMD_TERM_WINDOW:
            break;
        default:
            break;
    }
}

bool motion_event_filter_func(const GameActivityMotionEvent *motionEvent)
{
    auto sourceClass = motionEvent->source & AINPUT_SOURCE_CLASS_MASK;
    return (sourceClass == AINPUT_SOURCE_CLASS_POINTER ||
            sourceClass == AINPUT_SOURCE_CLASS_JOYSTICK);
}

void encoder_loop()
{
    LOGI("Encoder loop started");
    const int width = 1920;
    const int height = 1080;
    auto rgba = std::vector<uint8_t>(width * height * 4, 0);
    auto video_encoder = std::make_unique<VideoEncoder>(width, height, 10 << 20, 30);
    if (!video_encoder->create())
    {
        LOGE("Failed to create encoder");
        return;
    }
    auto audio_encoder = std::make_unique<AudioEncoder>(48000, 2, 1 << 20);
    if (!audio_encoder->create())
    {
        LOGE("Failed to create audio encoder");
        return;
    }

    // Youtube
    const std::string host = "a.rtmp.youtube.com";
    const std::string app = "live2";
    const std::string key = YT_KEY;

    rtmp::Socket socket;
    socket.connect_host(host, 1935);
    socket.handshake();
    socket.start_receiving();
    socket.send_connect_command(app);
    socket.send_create_stream_command(key);
    socket.send_publish_command(key);
    LOGI("Connected");
    socket.send_chunk_size();
    LOGI("SetChunkSize");
    video_encoder->on_config([&](std::span<const uint8_t> sps, std::span<const uint8_t> pps){
        LOGI("Got video config");
        socket.send_video_header(sps, pps);
    });
    video_encoder->on_packet([&](std::vector<std::span<const uint8_t>> nals, uint64_t pts_ms, bool keyframe){
        LOGI("Got video packet");
        socket.send_video_h264(nals, pts_ms, keyframe);
    });
    audio_encoder->on_config([&](std::span<const uint8_t> config){
        LOGI("Got audio config");
        socket.send_audio_header(config);
    });
    audio_encoder->on_packet([&](std::span<const uint8_t> data, uint64_t pts_ms){
        LOGI("Got audio packet");
        socket.send_audio_aac(data, pts_ms);
    });

    auto start = std::chrono::high_resolution_clock::now();
    auto pcm = std::vector<int16_t>(audio_encoder->max_input_samples(), 0);
    float audio_frame_time = (float)(audio_encoder->max_input_samples() / 2) / (float)audio_encoder->get_samplerate();
    uint64_t video_frames = 0;
    uint64_t audio_frames = 0;
    while (true)
    {
        auto diff = std::chrono::high_resolution_clock::now() - start;
        int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(diff).count();
        //LOGI("time %llu", ms);
        int video_time_ms = video_frames * 1000 / 30;
        if (ms > video_time_ms)
        {
            if (video_encoder->send_frame(rgba, ms))
                video_frames++;
        }
        uint64_t audio_time_ms = audio_frames * 1000 / audio_encoder->get_samplerate();
        //LOGI("audio time %lu", ms);
        if (ms > audio_time_ms)
        {
            if (audio_encoder->send_frame(pcm, audio_time_ms))
                audio_frames += pcm.size() / 2;
        }
        while (video_encoder->receive_packet());
        while (audio_encoder->receive_packet());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void android_main(android_app *pApp)
{
    auto context = std::make_unique<AndroidContext>(pApp);
    context->create();

    __android_log_print(ANDROID_LOG_INFO, "android_main", "android_main()");

    // Register an event handler for Android events
    pApp->onAppCmd = handle_cmd;
    pApp->userData = context.get();

    // Set input event filters (set it to NULL if the app wants to process all inputs).
    // Note that for key inputs, this example uses the default default_key_filter()
    // implemented in android_native_app_glue.c.
    android_app_set_motion_event_filter(pApp, motion_event_filter_func);

    auto encoder_thread = std::thread(&encoder_loop);
    context->main_loop();
}
