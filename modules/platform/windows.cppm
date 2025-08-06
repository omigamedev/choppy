module;

#include <print>
#include <memory>
#include <functional>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

export module ce.platform.win32;
import ce.platform;

LRESULT CALLBACK WindowProc(HWND hwnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam);

export namespace ce::platform
{
class Win32Window final : public Window
{
    HWND m_hWnd = nullptr;
    friend LRESULT CALLBACK ::WindowProc(HWND hwnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam);
    LRESULT HandleWindowProc(HWND hWnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam) const
    {
        switch (uMsg)
        {
        case WM_CREATE:
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        case WM_SIZE:
            std::print("YOU SHALL NOT RESIZE");
            if (on_resize)
            {
                const int32_t x = GET_X_LPARAM(lParam);
                const int32_t y = GET_Y_LPARAM(lParam);
                on_resize(x, y);
            }
            break;
        case WM_MOUSEWHEEL:
            if (on_mouse_wheel)
            {
                const uint16_t fwKeys = GET_KEYSTATE_WPARAM(wParam);
                const int16_t zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
                const int32_t x = GET_X_LPARAM(lParam);
                const int32_t y = GET_Y_LPARAM(lParam);
                const float norm_delta = static_cast<float>(zDelta) / static_cast<float>(WHEEL_DELTA);
                on_mouse_wheel(x, y, norm_delta);
            }
            break;
        case WM_MOUSEMOVE:
            if (on_mouse_move)
            {
                const int32_t x = GET_X_LPARAM(lParam);
                const int32_t y = GET_Y_LPARAM(lParam);
                on_mouse_move(x, y);
            }
            break;
        case WM_LBUTTONDOWN:
            if (on_mouse_left_down)
            {
                const int32_t x = GET_X_LPARAM(lParam);
                const int32_t y = GET_Y_LPARAM(lParam);
                on_mouse_left_down(x, y);
                SetCapture(m_hWnd);
            }
            break;
        case WM_LBUTTONUP:
            if (on_mouse_left_up)
            {
                const int32_t x = GET_X_LPARAM(lParam);
                const int32_t y = GET_Y_LPARAM(lParam);
                on_mouse_left_up(x, y);
                ReleaseCapture();
            }
            break;
        case WM_RBUTTONDOWN:
            if (on_mouse_right_down)
            {
                const int32_t x = GET_X_LPARAM(lParam);
                const int32_t y = GET_Y_LPARAM(lParam);
                on_mouse_right_down(x, y);
                SetCapture(m_hWnd);
            }
            break;
        case WM_RBUTTONUP:
            if (on_mouse_right_up)
            {
                const int32_t x = GET_X_LPARAM(lParam);
                const int32_t y = GET_Y_LPARAM(lParam);
                on_mouse_right_up(x, y);
                ReleaseCapture();
            }
            break;
        case WM_KEYDOWN:
            if (on_key_down)
            {
                on_key_down(wParam);
            }
            break;
        case WM_KEYUP:
            if (on_key_up)
            {
                on_key_up(wParam);
            }
        }
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
public:
    std::function<void(const int32_t x, const int32_t y)> on_mouse_left_down;
    std::function<void(const int32_t x, const int32_t y)> on_mouse_left_up;
    std::function<void(const int32_t x, const int32_t y)> on_mouse_right_down;
    std::function<void(const int32_t x, const int32_t y)> on_mouse_right_up;
    std::function<void(const int32_t x, const int32_t y)> on_mouse_move;
    std::function<void(const int32_t x, const int32_t y, const float delta)> on_mouse_wheel;
    std::function<void(const uint64_t keycode)> on_key_down;
    std::function<void(const uint64_t keycode)> on_key_up;
    std::function<void(const uint32_t width, const uint32_t height)> on_resize;
    bool create(const uint32_t width, const uint32_t height) noexcept override
    {
        constexpr wchar_t CLASS_NAME[] = L"ChoppyWindowClass";
        const HINSTANCE hInstance = GetModuleHandle(nullptr);
        const WNDCLASS wc{
            .lpfnWndProc = WindowProc,
            .hInstance = hInstance,
            .lpszClassName = CLASS_NAME,
        };
        if (!RegisterClass(&wc))
        {
            // TODO: add some proper logging please
            return false;
        }
        constexpr DWORD style = WS_OVERLAPPEDWINDOW;
        constexpr DWORD ex_style = 0;
        RECT rect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
        AdjustWindowRectEx(&rect, style, FALSE, ex_style);
        const int windowWidth = rect.right - rect.left;
        const int windowHeight = rect.bottom - rect.top;
        m_hWnd = CreateWindowEx(
            ex_style,
            CLASS_NAME,
            L"Choppy Engine",
            style,
            CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight,
            nullptr, nullptr, hInstance, this
        );
        if (!m_hWnd)
        {
            return false;
        }
        ShowWindow(m_hWnd, SW_SHOW);
        m_width = width;
        m_height = height;
        return true;
    }
    [[nodiscard]] HWND hwnd() const noexcept
    {
        return m_hWnd;
    }
};
class Win32 final : public Platform
{
public:
    [[nodiscard]] std::shared_ptr<Window> new_window() const noexcept override
    {
        return std::make_shared<Win32Window>();
    }
    [[nodiscard]] HINSTANCE hinstance() const noexcept
    {
        return GetModuleHandle(nullptr);
    }
};
}

LRESULT CALLBACK WindowProc(HWND hwnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
{
    using namespace ce::platform;
    // store the pointer to the context
    [[unlikely]] if (uMsg == WM_NCCREATE)
    {
        const CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        const void* ptr = cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ptr));
    }
    else if (Win32Window* context = reinterpret_cast<Win32Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA)))
    {
        return context->HandleWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
