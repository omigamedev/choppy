#include <memory>
#include <print>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <volk.h>

import ce.platform_windows;
import ce.xr;
import ce.vk;

class WindowsContext
{
    std::unique_ptr<ce::platform::PlatformWindows> platform;
    ce::xr::Instance xr_instance;
    ce::vk::Context vk_context;
    HWND m_wnd = nullptr;
    bool create_window()
    {
        //WNDCLASS wc
    }
public:
    bool create()
    {
        auto platform = std::make_unique<ce::platform::PlatformWindows>();
        if (xr_instance.create())
        {
            std::println("OpenXR initialized");
            vk_context.create_from(
                xr_instance.vk_instance(),
                xr_instance.device(),
                xr_instance.physical_device(),
                xr_instance.queue_family_index());
            std::println("Start XR session");
            if (!xr_instance.start_session())
            {
                std::println("Failed to start session");
                return false;
            }
            std::println("Create XR swapchain");
            if (!xr_instance.create_swapchain())
            {
                std::println("Failed to initialize swapchain");
                return false;
            }
            std::println("XR created succesfully");
        }
        else if (vk_context.create())
        {
            std::println("Failed to initialize OpenXR, using Vulkan");
        }
        else
        {
            std::println("Failed to initialize Vulkan and OpenXR");
            return false;
        }
        return true;
    }
    void tick()
    {

    }
    void destroy()
    {

    }
    void main_loop()
    {
        std::println("starting main loop");
        MSG msg;
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
            tick();
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
