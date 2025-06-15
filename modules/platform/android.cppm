module;
export module ce.platform.android;
import ce.platform;

export namespace ce::platform::android
{
class PlatformAndroid : public IPlatform
{

};
}

using namespace ce::platform;
#ifdef __ANDROID__
IPlatform& IPlatform::Get()
{
    static IPlatform* platform = new android::PlatformAndroid();
    return *platform;
}
#endif
