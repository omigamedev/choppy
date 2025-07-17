module;
export module ce.platform.globals;
import ce.platform;

#ifdef WIN32
import ce.platform.win32;
ce::platform::Win32 platform_storage{};
#elifdef __ANDROID__
import ce.platform.android;
ce::platform::Android platform_storage;
#endif

export namespace ce::platform
{
    template<typename T = Platform>
    T& GetPlatform() noexcept
    {
        return static_cast<T&>(platform_storage);
    }
}
