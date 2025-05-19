#include <lib.h>
#include <cstdint>
#include <thread>
#include <chrono>
#include <string>
#include <string_view>

#include <jni.h>
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <video_encoder.h>
#include <rtmp.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)

import std;
import vulkan_hpp;

namespace vk::detail { DispatchLoaderDynamic defaultDispatchLoaderDynamic; }

//extern "C" void android_main(struct android_app* state);
extern "C" int foo(int a, int b);

void handle_cmd(android_app *pApp, int32_t cmd)
{
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
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
    auto video_encoder = std::make_unique<VideoEncoder>(width, height, 10 << 20, 30);
    if (!video_encoder->create())
    {
        LOGE("Failed to create encoder");
        return;
    }
    auto start = std::chrono::high_resolution_clock::now();
    auto rgba = std::vector<uint8_t>(width * height * 4, 0);

    // Youtube
    const std::string host = "a.rtmp.youtube.com";
    const std::string app = "live2";
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
    video_encoder->on_config([&socket](std::vector<uint8_t> sps, std::vector<uint8_t> pps){
        LOGI("Got config");
        socket.send_video_header(sps, pps);
    });

    while (true)
    {
        auto diff = std::chrono::high_resolution_clock::now() - start;
        int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(diff).count();
        video_encoder->send_frame(rgba, ms);
        uint64_t pts = 0;
        std::vector<uint8_t> pkt;
        if (video_encoder->receive_packet(pkt, pts))
        {
            LOGI("Got packet");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / 30));
    }
}

void android_main(struct android_app *pApp)
{
    //auto n = lib::foo(3, 2);
    //lib::prints("hello");
    //int s = foo(1, 2);
    //vk::detail::defaultDispatchLoaderDynamic.init();
    //std::uint32_t v = vk::enumerateInstanceVersion();
    //std::println("Found Vulkan runtime {}.{}.{}", vk::versionMajor(v), vk::versionMinor(v), vk::versionPatch(v));
    //const auto app_info = vk::ApplicationInfo{ "VulkanApp", 0, nullptr, 0, vk::ApiVersion14 };
    //const auto instance_info = vk::InstanceCreateInfo{ vk::InstanceCreateFlags{}, &app_info, };
    //auto instance = vk::createInstanceUnique(instance_info);
    //vk::detail::defaultDispatchLoaderDynamic.init(*instance);

    __android_log_print(ANDROID_LOG_INFO, "android_main", "android_main()");

    // Register an event handler for Android events
    pApp->onAppCmd = handle_cmd;

    // Set input event filters (set it to NULL if the app wants to process all inputs).
    // Note that for key inputs, this example uses the default default_key_filter()
    // implemented in android_native_app_glue.c.
    android_app_set_motion_event_filter(pApp, motion_event_filter_func);

    auto encoder_thread = std::thread(&encoder_loop);

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
