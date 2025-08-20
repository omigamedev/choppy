#include <memory>
#include <print>
#define WIN32_LEAN_AND_MEAN
#include <chrono>
#include <windows.h>
#include <volk.h>
#include <GameInput.h>

import ce.app;
import ce.xr;
import ce.vk;
import ce.platform;
import ce.platform.globals;
import ce.platform.win32;

std::string hresult_to_string(const HRESULT hr)
{
    char* msgBuffer = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        hr,
        0, // Default language
        msgBuffer,
        0,
        nullptr
    );

    const std::string message = (size && msgBuffer) ? std::string(msgBuffer) : "Unknown error";

    if (msgBuffer)
        LocalFree(msgBuffer);

    return message;
}

class WindowsContext
{
    ce::app::AppBase app;
    HWND m_wnd = nullptr;
    bool initialized = false;
    std::shared_ptr<ce::platform::Win32Window> m_window;
    IGameInput* gameInput = nullptr;
    IGameInputDevice* device = nullptr;
    bool create_window() noexcept
    {
        const auto& win32 = ce::platform::GetPlatform<ce::platform::Win32>();
        m_window = std::static_pointer_cast<ce::platform::Win32Window>(win32.new_window());
        return m_window->create(1024, 1024);
    }
public:
    bool create()
    {
        const auto& xr = app.xr() = std::make_shared<ce::xr::Context>();
        const auto& vk = app.vk() = std::make_shared<ce::vk::Context>();
        bool xr_mode = false;
        if (xr->create())
        {
            std::println("OpenXR initialized");
            vk->create_from(
                xr->vk_instance(),
                xr->device(),
                xr->physical_device(),
                xr->queue_family_index());
            std::println("Start XR session");
            xr->create_vulkan_objects(vk);
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
            xr_mode = true;
            xr->bind_input();
        }
        else if (create_window() && vk->create(m_window))
        {
            std::println("Failed to initialize OpenXR, using Vulkan");
            if (!vk->create_swapchain())
            {
                std::println("Failed to create swapchain");
                return false;
            }
            if (!vk->create_renderpass())
            {
                std::println("Failed to create renderpass");
                return false;
            }
            if (!vk->create_framebuffer())
            {
                std::println("Failed to create framebuffer");
                return false;
            }
            m_window->on_resize = [this](const uint32_t width, const uint32_t height)
            {
                if (width != 0 && height != 0)
                {
                    app.on_resize(width, height);
                }
            };
            m_window->on_mouse_move = [this](const int32_t x, const int32_t y)
            {
                app.on_mouse_move(x, y);
            };
            m_window->on_mouse_wheel = [this](const int32_t x, const int32_t y, const float delta)
            {
                app.on_mouse_wheel(x, y, delta);
            };
            m_window->on_mouse_left_down = [this](const int32_t x, const int32_t y)
            {
                app.on_mouse_left_down(x, y);
            };
            m_window->on_mouse_left_up = [this](const int32_t x, const int32_t y)
            {
                app.on_mouse_left_up(x, y);
            };
            m_window->on_mouse_right_down = [this](const int32_t x, const int32_t y)
            {
                app.on_mouse_right_down(x, y);
            };
            m_window->on_mouse_right_up = [this](const int32_t x, const int32_t y)
            {
                app.on_mouse_right_up(x, y);
            };
            m_window->on_key_down = [this](const uint64_t keycode)
            {
                app.on_key_down(keycode);
            };
            m_window->on_key_up = [this](const uint64_t keycode)
            {
                app.on_key_up(keycode);
            };
            xr_mode = false;
        }
        else
        {
            std::println("Failed to initialize Vulkan and OpenXR");
            return false;
        }
        app.init(xr_mode);
        if (!xr_mode)
        {
            app.on_resize(m_window->width(), m_window->height());
        }
        if (const HRESULT result = GameInputCreate(&gameInput); FAILED(result))
        {
            std::println("Failed to create GameInput: {}", hresult_to_string(result));
        }
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
                ce::app::GamepadState gamepad;
                if (gameInput)
                {
                    // Enumerate devices
                    IGameInputReading* reading = nullptr;
                    if (const HRESULT result = gameInput->GetCurrentReading(GameInputKindGamepad, nullptr, &reading);
                        SUCCEEDED(result) && reading)
                    {
                        GameInputGamepadState state{};
                        reading->GetGamepadState(&state);
                        //printf("Left stick X: %f\n", state.leftThumbstickX);
                        gamepad = ce::app::GamepadState{
                            .buttons = {
                                static_cast<bool>(state.buttons & GameInputGamepadMenu),
                                static_cast<bool>(state.buttons & GameInputGamepadView),
                                static_cast<bool>(state.buttons & GameInputGamepadA),
                                static_cast<bool>(state.buttons & GameInputGamepadB),
                                static_cast<bool>(state.buttons & GameInputGamepadX),
                                static_cast<bool>(state.buttons & GameInputGamepadY),
                                static_cast<bool>(state.buttons & GameInputGamepadDPadUp),
                                static_cast<bool>(state.buttons & GameInputGamepadDPadDown),
                                static_cast<bool>(state.buttons & GameInputGamepadDPadLeft),
                                static_cast<bool>(state.buttons & GameInputGamepadDPadRight),
                                static_cast<bool>(state.buttons & GameInputGamepadRightShoulder),
                                static_cast<bool>(state.buttons & GameInputGamepadLeftShoulder),
                                static_cast<bool>(state.buttons & GameInputGamepadLeftThumbstick),
                                static_cast<bool>(state.buttons & GameInputGamepadRightThumbstick),
                            },
                            .thumbstick_left = {state.leftThumbstickX, state.leftThumbstickY},
                            .thumbstick_right = {state.rightThumbstickX, state.rightThumbstickY},
                            .trigger_left = state.leftTrigger,
                            .trigger_right = state.rightTrigger,
                        };
                        reading->Release();
                    }
                }

                const auto current_time = std::chrono::high_resolution_clock::now();
                const float delta_time = std::chrono::duration<float>(current_time - start_time).count();
                start_time = current_time;
                app.tick(delta_time, gamepad);
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
