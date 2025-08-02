module;

#include <memory>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

export module ce.platform.win32;
import ce.platform;

LRESULT CALLBACK WindowProc(HWND hwnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

export namespace ce::platform
{
class Win32Window final : public Window
{
    HWND m_hWnd = nullptr;
public:
    bool create() noexcept override
    {
        constexpr wchar_t CLASS_NAME[] = L"ChoppyWindowClass";
        const HINSTANCE hInstance = GetModuleHandle(nullptr);
        const WNDCLASS wc = {
            .lpfnWndProc = WindowProc,
            .hInstance = hInstance,
            .lpszClassName = CLASS_NAME,
        };

        if (!RegisterClass(&wc))
        {
            // TODO: add some proper logging please
            return false;
        }

        m_hWnd = CreateWindowEx(
            0,
            CLASS_NAME,
            L"Hello Choppy Engine",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 500, 300,
            nullptr, nullptr, hInstance, nullptr
        );

        if (!m_hWnd)
        {
            return false;
        }

        ShowWindow(m_hWnd, SW_SHOW);
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
    [[nodiscard]] std::shared_ptr<Window> create_window() const noexcept override
    {
        return std::make_shared<Win32Window>();
    }
    [[nodiscard]] HINSTANCE hinstance() const noexcept
    {
        return GetModuleHandle(nullptr);
    }
};
}
