module;
export module ce.platform.windows;
import ce.platform;


export namespace ce::platform::windows
{
class PlatformWindows : public IPlatform
{

};
}

using namespace ce::platform;
#ifdef _WIN32
IPlatform& IPlatform::Get()
{
    static IPlatform* platform = new windows::PlatformWindows();
    return *platform;
}
#endif
