#include <memory>
#include <print>
#define WIN32_LEAN_AND_MEAN
#include <chrono>
#include <windows.h>
#include <volk.h>

import ce.app;
import ce.xr;
import ce.vk;

class WindowsContext
{
    ce::app::AppBase app;
    HWND m_wnd = nullptr;
    bool initialized = false;
    bool create_window()
    {
        //WNDCLASS wc
    }
public:
    bool create()
    {
        const auto& xr = app.xr() = std::make_shared<ce::xr::Context>();
        const auto& vk = app.vk() = std::make_shared<ce::vk::Context>();
        if (xr->create())
        {
            std::println("OpenXR initialized");
            vk->create_from(
                xr->vk_instance(),
                xr->device(),
                xr->physical_device(),
                xr->queue_family_index());
            std::println("Start XR session");
            if (!xr->create_session())
            {
                std::println("Failed to create session");
                return false;
            }
            std::println("Create XR swapchain");
            if (!xr->create_swapchain())
            {
                std::println("Failed to initialize swapchain");
                return false;
            }
            if (!xr->begin_session())
            {
                std::println("Failed to begin session");
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
                const auto current_time = std::chrono::high_resolution_clock::now();
                const float delta_time = std::chrono::duration<float>(current_time - start_time).count();
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
