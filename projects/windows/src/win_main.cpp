#include <memory>
#include <print>
#define WIN32_LEAN_AND_MEAN
#include <chrono>
#include <windows.h>
#include <volk.h>

import ce.windows;
import ce.app;
import ce.xr;
import ce.vk;

class WindowsContext
{
    ce::app::AppBase app;
    std::unique_ptr<ce::platform::Windows> platform;
    HWND m_wnd = nullptr;
    bool initialized = false;
    bool create_window()
    {
        //WNDCLASS wc
    }
public:
    bool create()
    {
        app.platform() = std::make_unique<ce::platform::Windows>();
        auto& xr = app.xr() = std::make_shared<ce::xr::Context>();
        auto& vk = app.vk() = std::make_shared<ce::vk::Context>();
        if (xr->create())
        {
            std::println("OpenXR initialized");
            vk->create_from(
                xr->vk_instance(),
                xr->device(),
                xr->physical_device(),
                xr->queue_family_index());
            std::println("Start XR session");
            if (!xr->start_session())
            {
                std::println("Failed to start session");
                return false;
            }
            std::println("Create XR swapchain");
            if (!xr->create_swapchain())
            {
                std::println("Failed to initialize swapchain");
                return false;
            }
            std::println("XR created succesfully");
        }
        else if (vk->create())
        {
            std::println("Failed to initialize OpenXR, using Vulkan");
        }
        else
        {
            std::println("Failed to initialize Vulkan and OpenXR");
            return false;
        }
        app.init();
        initialized = true;
        return true;
    }
    void destroy()
    {

    }
    void main_loop()
    {
        std::println("starting main loop");
        MSG msg;
        static auto start_time = std::chrono::high_resolution_clock::now();
        while (true)
        {
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
                if (msg.message == WM_QUIT)
                {
                    destroy();
                    return;
                }
            }
            if (initialized)
            {
                auto current_time = std::chrono::high_resolution_clock::now();
                float delta_time = std::chrono::duration<float>(current_time - start_time).count();
                start_time = current_time;
                app.tick(delta_time);
            }
        }
    }
};
int main()
{
    WindowsContext context;
    context.create();
    context.main_loop();
    return 0;
}
