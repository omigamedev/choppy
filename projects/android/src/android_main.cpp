#include <memory>
#include <semaphore>
#include <mutex>
#include <future>

#include <jni.h>
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/choreographer.h>
#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <paddleboat/paddleboat.h>
#include <video_encoder.h>
#include <audio_encoder.h>
#include <rtmp.h>
#include <volk.h>

#include <tracy/Tracy.hpp>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)

import ce.vk;
import ce.xr;
import ce.app;
import ce.platform;
import ce.platform.android;
import ce.platform.globals;

using namespace ce;

void* operator new(const std::size_t count)
{
    const auto ptr = malloc(count);
    TracyAlloc(ptr, count);
    return ptr;
}
void operator delete(void* ptr) noexcept
{
    TracyFree(ptr);
    free(ptr);
}

void vsyncCallback(long frameTimeNanos, void* data)
{
    FrameMarkNamed("ScreenRefresh");   // Marks a frame in Tracy

    // Reschedule callback for next vsync
    AChoreographer* choreographer = AChoreographer_getInstance();
    AChoreographer_postFrameCallback64(choreographer, vsyncCallback, data);
}

void initVsync()
{
    AChoreographer* choreographer = AChoreographer_getInstance();
    AChoreographer_postFrameCallback64(choreographer, vsyncCallback, nullptr);
}

static JavaVM* g_java_vm = nullptr;
static jclass g_main_activity_class = nullptr;
static jmethodID g_request_permissions_method_id = nullptr;

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    LOGI("JNI_OnLoad called");
    g_java_vm = vm;

    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
    {
        LOGE("Failed to get JNI environment");
        return JNI_ERR;
    }

    jclass local_main_activity_class = env->FindClass("com/omixlab/cubey/MainActivity");
    if (local_main_activity_class == nullptr)
    {
        LOGE("Failed to find class com/omixlab/cubey/MainActivity");
        return JNI_ERR;
    }

    g_main_activity_class = reinterpret_cast<jclass>(env->NewGlobalRef(local_main_activity_class));
    env->DeleteLocalRef(local_main_activity_class);

    if (g_main_activity_class == nullptr)
    {
        LOGE("Failed to create global reference for MainActivity class");
        return JNI_ERR;
    }

    g_request_permissions_method_id = env->GetMethodID(g_main_activity_class,
        "requestPermissions", "()V");
    if (g_request_permissions_method_id == nullptr)
    {
        LOGE("Failed to find method 'requestPermissions' with signature '()V'");
        return JNI_ERR;
    }

    LOGI("Successfully cached JNI references");
    return JNI_VERSION_1_6;
}

static std::promise<bool> permission_promise;

extern "C" JNIEXPORT void JNICALL
Java_com_omixlab_cubey_MainActivity_onRequestPermissionsResultNative(JNIEnv* env,
    jobject thiz, jboolean record_audio)
{
    permission_promise.set_value(record_audio);
}

std::future<bool> check_permissions(JNIEnv *env, const GameActivity* activity)
{
    // A promise can only be set once. Create a new one for each request.
    permission_promise = std::promise<bool>();

    if (g_main_activity_class == nullptr || g_request_permissions_method_id == nullptr) {
        LOGE("JNI references not cached! Cannot request permissions.");
        permission_promise.set_value(false); // Signal failure immediately
        return permission_promise.get_future();
    }

    // Now just call the method using the cached ID. This is fast and thread-safe.
    LOGI("Calling requestPermissions using cached method ID.");
    env->CallVoidMethod(activity->javaGameActivity, g_request_permissions_method_id);

    return permission_promise.get_future();
}

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
            xr->create_vulkan_objects(vk);
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
        xr->bind_input();
        app.init(true, false, false);
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
    static void paddleboatControllerStatusCallback(const int32_t controllerIndex,
        const Paddleboat_ControllerStatus controllerStatus, void *userData)
    {
        LOGI("Controller %d connected: %d", controllerIndex,
            controllerStatus & PADDLEBOAT_CONTROLLER_JUST_CONNECTED);
    }
    void check_permissions_and_wait() noexcept
    {
        std::future<bool> permissions_results = check_permissions(m_env, pApp->activity);
        LOGI("Waiting for permission");
        int events;
        android_poll_source *pSource = nullptr;
        while (ALooper_pollOnce(-1, nullptr, &events, (void **) &pSource) >= 0)
        {
            if (pSource)
            {
                pSource->process(pApp, pSource);
            }
            if (permissions_results.valid())
                break;
        }
    }
    bool attach_vm() noexcept
    {
        if (pApp->activity->vm->AttachCurrentThread(&m_env, NULL) != JNI_OK)
        {
            LOGE("Failed to attach current thread");
            return false;
        }
        return true;
    }
    void update_input() noexcept
    {
        static uint64_t mActiveAxisIds = 0;
        const uint64_t activeAxisIds = Paddleboat_getActiveAxisMask();
        uint64_t newAxisIds = activeAxisIds ^ mActiveAxisIds;
        if (newAxisIds != 0) {
            mActiveAxisIds = activeAxisIds;
            int32_t currentAxisId = 0;
            while(newAxisIds != 0) {
                if ((newAxisIds & 1) != 0) {
                    LOGI("Enable Axis: %d", currentAxisId);
                    GameActivityPointerAxes_enableAxis(currentAxisId);
                }
                ++currentAxisId;
                newAxisIds >>= 1;
            }
        }

        if (android_input_buffer* inputBuffer = android_app_swap_input_buffers(pApp))
        {
            if (inputBuffer->keyEventsCount != 0)
            {
                for (uint64_t i = 0; i < inputBuffer->keyEventsCount; ++i)
                {
                    const GameActivityKeyEvent* keyEvent = &inputBuffer->keyEvents[i];
                    if (!Paddleboat_processGameActivityKeyInputEvent(
                        keyEvent, sizeof(GameActivityKeyEvent)))
                    {
                        LOGE("KeyEvent not processed by Paddleboat");
                    }
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
                        // Log if Paddleboat didn't handle it
                        LOGE("MotionEvent not processed by Paddleboat, source: %d",
                            motionEvent->source);
                    }
                }
                android_app_clear_motion_events(inputBuffer);
            }
        }
    }
    void update_app() noexcept
    {
        static auto start_time = std::chrono::high_resolution_clock::now();
        if (!session_started)
            return;

        auto current_time = std::chrono::high_resolution_clock::now();
        float delta_time = std::chrono::duration<float>(current_time - start_time).count();
        start_time = current_time;
        Paddleboat_update(m_env);

        ce::app::GamepadState gamepad;
        Paddleboat_Controller_Data controllerData;
        if (Paddleboat_getControllerData(0, &controllerData) == PADDLEBOAT_NO_ERROR)
        {
            gamepad = ce::app::GamepadState{
                .buttons = {
                    static_cast<bool>(controllerData.buttonsDown & PADDLEBOAT_BUTTON_SYSTEM),
                    static_cast<bool>(controllerData.buttonsDown & PADDLEBOAT_BUTTON_SELECT),
                    static_cast<bool>(controllerData.buttonsDown & PADDLEBOAT_BUTTON_A),
                    static_cast<bool>(controllerData.buttonsDown & PADDLEBOAT_BUTTON_B),
                    static_cast<bool>(controllerData.buttonsDown & PADDLEBOAT_BUTTON_X),
                    static_cast<bool>(controllerData.buttonsDown & PADDLEBOAT_BUTTON_Y),
                    static_cast<bool>(controllerData.buttonsDown & PADDLEBOAT_BUTTON_DPAD_UP),
                    static_cast<bool>(controllerData.buttonsDown & PADDLEBOAT_BUTTON_DPAD_DOWN),
                    static_cast<bool>(controllerData.buttonsDown & PADDLEBOAT_BUTTON_DPAD_LEFT),
                    static_cast<bool>(controllerData.buttonsDown & PADDLEBOAT_BUTTON_DPAD_RIGHT),
                    //static_cast<bool>(controllerData.buttonsDown & GameInputGamepadRightShoulder),
                    //static_cast<bool>(controllerData.buttonsDown & GameInputGamepadLeftShoulder),
                    //static_cast<bool>(controllerData.buttonsDown & GameInputGamepadLeftThumbstick),
                    //static_cast<bool>(controllerData.buttonsDown & GameInputGamepadRightThumbstick),
                },
                .thumbstick_left = {controllerData.leftStick.stickX, controllerData.leftStick.stickY},
                .thumbstick_right = {controllerData.rightStick.stickX, controllerData.rightStick.stickY},
                .trigger_left = controllerData.triggerL1,
                .trigger_right = controllerData.triggerR1,
            };
        }
        app.tick(delta_time, gamepad);
    }
    void main_loop() noexcept
    {
        if (Paddleboat_init(m_env, pApp->activity->javaGameActivity) != PADDLEBOAT_NO_ERROR)
        {
            LOGE("Paddleboat_init failed");
        }
        //Paddleboat_setMotionDataPrecision(PADDLEBOAT_MOTION_PRECISION_FLOAT); // Good practice
        Paddleboat_setControllerStatusCallback(
            &AndroidContext::paddleboatControllerStatusCallback, nullptr);

        Paddleboat_Controller_Info controllerInfo;
        if (Paddleboat_getControllerInfo(0, &controllerInfo) == PADDLEBOAT_NO_ERROR)
        {
            char name[1024];
            Paddleboat_getControllerName(0, sizeof(name), name);
            LOGI("Controller %d Info: Name: %s, Flags=0x%X, DeviceId=%d, VID=0x%X, PID=0x%X",
                0, name, controllerInfo.controllerFlags, controllerInfo.deviceId,
                controllerInfo.vendorId, controllerInfo.productId);
        }

        int events;
        android_poll_source *pSource;
        do
        {
            // Process all pending events before running game logic.
            while (ALooper_pollOnce(0, nullptr, &events, (void **) &pSource) >= 0)
            {
                if (pSource)
                {
                    pSource->process(pApp, pSource);
                }
            }
            update_input();
            update_app();
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
                LOGI("paddleboat started");
            }
            break;
        case APP_CMD_STOP:
            if (auto context = reinterpret_cast<AndroidContext*>(pApp->userData))
            {
                Paddleboat_onStop(context->env());
                LOGI("paddleboat stopped");
            }
            break;
        case APP_CMD_INIT_WINDOW:
            LOGI("APP_CMD_INIT_WINDOW");
            if (auto context = reinterpret_cast<AndroidContext*>(pApp->userData))
            {
                context->begin_session();
                initVsync();
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
    if (!context.attach_vm())
    {
        GameActivity_finish(pApp->activity);
        return;
    }
    context.check_permissions_and_wait();
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
