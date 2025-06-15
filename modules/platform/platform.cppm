module;
export module ce.platform;
import std;

export namespace ce::platform
{
class Window
{
public:
};
class IPlatform
{
public:
    static IPlatform& Get();
    static std::shared_ptr<Window> CreateWindow(int width, int height);
};
}
