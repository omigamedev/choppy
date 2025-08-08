#include <jni.h>
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <paddleboat/paddleboat.h>
#include <video_encoder.h>
#include <audio_encoder.h>
#include <rtmp.h>
#include <memory>
#include <volk.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)

import ce.vk;
import ce.xr;
import ce.app;
import ce.platform;
import ce.platform.android;
import ce.platform.globals;

using namespace ce;

class AndroidContext
{
    app::AppBase app;
    android_app *pApp;
    bool session_started = false;
    JNIEnv* m_env{nullptr};
public:
    AndroidContext(android_app *pApp) noexcept : pApp(pApp) { }
    JNIEnv* env() noexcept { return m_env; };
    bool create() noexcept
    {
        auto& platform = platform::GetPlatform<platform::Android>();
        if (!platform.setup_android(pApp))
        {
            LOGE("setup_android failed");
            return false;
        }
        const auto& xr = app.xr() = std::make_shared<xr::Context>();
        const auto& vk = app.vk() = std::make_shared<vk::Context>();
        if (!xr->setup_android(pApp->activity->vm, pApp->activity->javaGameActivity))
        {
            LOGE("xr->setup_android failed");
            return false;
        }
        if (xr->create())
        {
            vk->create_from(
                xr->vk_instance(),
                xr->device(),
                xr->physical_device(),
                xr->queue_family_index());
        }
        else
        {
            LOGE("XR initialization failed, exiting");
            return false;
        }
        return true;
    }
    bool create_session() noexcept
    {
        auto& xr = app.xr();
        auto& vk = app.vk();
        LOGI("Creating session");
        if (!xr->create_session())
        {
            LOGE("Failed to start session");
            return false;
        }
        LOGI("Creating swapchain");
        if (!xr->create_swapchain())
        {
            LOGE("Failed to create swapchain");
            return false;
        }
        app.init(true);
        return true;
    }
    bool begin_session() noexcept
    {
        auto& xr = app.xr();
        LOGI("Begin session");
        if (!xr->begin_session())
        {
            LOGE("Failed to begin session");
            return false;
        }
        session_started = true;
        return true;
    }
    bool end_session() noexcept
    {
        auto& xr = app.xr();
        LOGI("End session");
        if (!xr->end_session())
        {
            LOGE("Failed to begin session");
            return false;
        }
        session_started = false;
        return true;
    }
    void main_loop() noexcept
    {
        pApp->activity->vm->AttachCurrentThread(&m_env, NULL);
        if (Paddleboat_init(m_env, pApp->activity->javaGameActivity) != PADDLEBOAT_NO_ERROR)
        {
            LOGE("Paddleboat_init failed");
        }

        int events;
        android_poll_source *pSource;
        auto start_time = std::chrono::high_resolution_clock::now();
        do
        {
            // Process all pending events before running game logic.
            if (ALooper_pollOnce(0, nullptr, &events, (void **) &pSource) >= 0)
            {
                if (pSource)
                {
                    pSource->process(pApp, pSource);
                }
            }
            if (android_input_buffer* inputBuffer = android_app_swap_input_buffers(pApp))
            {
                if (inputBuffer->keyEventsCount != 0)
                {
                    for (uint64_t i = 0; i < inputBuffer->keyEventsCount; ++i)
                    {
                        const GameActivityKeyEvent* keyEvent = &inputBuffer->keyEvents[i];
                        Paddleboat_processGameActivityKeyInputEvent(keyEvent, sizeof(GameActivityKeyEvent));
                    }
                    android_app_clear_key_events(inputBuffer);
                }
                if (inputBuffer->motionEventsCount != 0)
                {
                    for (uint64_t i = 0; i < inputBuffer->motionEventsCount; ++i)
                    {
                        const GameActivityMotionEvent* motionEvent = &inputBuffer->motionEvents[i];
                        if (!Paddleboat_processGameActivityMotionInputEvent(motionEvent,
                            sizeof(GameActivityMotionEvent)))
                        {
                            // Didn't belong to a game controller, process it ourselves if it is a touch event
                            // _cook_game_activity_motion_event(motionEvent, _cooked_event_callback);
                        }
                    }
                    android_app_clear_motion_events(inputBuffer);
                }
            }

            auto current_time = std::chrono::high_resolution_clock::now();
            float delta_time = std::chrono::duration<float>(current_time - start_time).count();
            start_time = current_time;
            if (session_started)
            {
                Paddleboat_update(m_env);

                int i = 0;
                Paddleboat_Controller_Data controllerData;
                if (Paddleboat_getControllerData(i, &controllerData) == PADDLEBOAT_NO_ERROR)
                {
                    // Controller 'i' is connected and data is available

                    // Check specific buttons
                    if (controllerData.buttonsDown & PADDLEBOAT_BUTTON_A) {
                        LOGI("Controller %d: Button A is pressed", i);
                    }
                    if (controllerData.buttonsDown & PADDLEBOAT_BUTTON_B) {
                        LOGI("Controller %d: Button B is pressed", i);
                    }
                    if (controllerData.buttonsDown & PADDLEBOAT_BUTTON_X) {
                        LOGI("Controller %d: Button X is pressed", i);
                    }
                    if (controllerData.buttonsDown & PADDLEBOAT_BUTTON_Y) {
                        LOGI("Controller %d: Button Y is pressed", i);
                    }
                    if (controllerData.buttonsDown & PADDLEBOAT_BUTTON_L1) {
                        LOGI("Controller %d: Button L1 is pressed", i);
                    }
                    if (controllerData.buttonsDown & PADDLEBOAT_BUTTON_R1) {
                        LOGI("Controller %d: Button R1 is pressed", i);
                    }
                    if (controllerData.buttonsDown & PADDLEBOAT_BUTTON_DPAD_UP) {
                        LOGI("Controller %d: D-pad Up is pressed", i);
                    }
                    // ... and so on for other buttons like:
                    // PADDLEBOAT_BUTTON_DPAD_DOWN, PADDLEBOAT_BUTTON_DPAD_LEFT, PADDLEBOAT_BUTTON_DPAD_RIGHT
                    // PADDLEBOAT_BUTTON_L2, PADDLEBOAT_BUTTON_R2 (these might also have trigger values)
                    // PADDLEBOAT_BUTTON_THUMBL, PADDLEBOAT_BUTTON_THUMBR
                    // PADDLEBOAT_BUTTON_START, PADDLEBOAT_BUTTON_SELECT (or BACK)
                    // PADDLEBOAT_BUTTON_HOME, PADDLEBOAT_BUTTON_TOUCHPAD (if available)

                    // Access analog stick values (joysticks)
                    // float leftStickX = controllerData.leftStick.stickX;
                    // float leftStickY = controllerData.leftStick.stickY;
                    // float rightStickX = controllerData.rightStick.stickX;
                    // float rightStickY = controllerData.rightStick.stickY;

                    // Access trigger values
                    // float L2_trigger = controllerData.triggerL2;
                    // float R2_trigger = controllerData.triggerR2;
                }

                app.tick(delta_time, {});
            }
        } while(!pApp->destroyRequested);
    }
};

void handle_cmd(android_app *pApp, int32_t cmd) noexcept
{
    switch (cmd) {
        case APP_CMD_START:
            if (auto context = reinterpret_cast<AndroidContext*>(pApp->userData))
            {
                Paddleboat_onStart(context->env());
            }
            break;
        case APP_CMD_STOP:
            if (auto context = reinterpret_cast<AndroidContext*>(pApp->userData))
            {
                Paddleboat_onStop(context->env());
            }
            break;
        case APP_CMD_INIT_WINDOW:
            LOGI("APP_CMD_INIT_WINDOW");
            if (auto context = reinterpret_cast<AndroidContext*>(pApp->userData))
            {
                context->begin_session();
            }
            break;
        case APP_CMD_TERM_WINDOW:
            LOGI("APP_CMD_TERM_WINDOW");
            if (auto context = reinterpret_cast<AndroidContext*>(pApp->userData))
            {
                context->end_session();
            }
            break;
        default:
            break;
    }
}

bool motion_event_filter_func(const GameActivityMotionEvent *motionEvent) noexcept
{
    auto sourceClass = motionEvent->source & AINPUT_SOURCE_CLASS_MASK;
    return (sourceClass == AINPUT_SOURCE_CLASS_POINTER ||
            sourceClass == AINPUT_SOURCE_CLASS_JOYSTICK);
}

void encoder_loop() noexcept
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
    AndroidContext context{pApp};
    if (!context.create())
    {
        GameActivity_finish(pApp->activity);
        return;
    }
    if (!context.create_session())
    {
        GameActivity_finish(pApp->activity);
        return;
    }

    LOGI("android_main");

    // Register an event handler for Android events
    pApp->onAppCmd = handle_cmd;
    pApp->userData = &context;

    // Set input event filters (set it to NULL if the app wants to process all inputs).
    // Note that for key inputs, this example uses the default default_key_filter()
    // implemented in android_native_app_glue.c.
    android_app_set_motion_event_filter(pApp, motion_event_filter_func);

    //auto encoder_thread = std::thread(&encoder_loop);
    context.main_loop();

    Paddleboat_destroy(pApp->activity->env);
}
