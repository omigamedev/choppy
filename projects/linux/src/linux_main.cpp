#include <print>
#include <memory>
#include <functional>

#include <tracy/Tracy.hpp>

import ce.app;
import ce.xr;
import ce.vk;
import ce.platform;
import ce.platform.globals;
//import ce.platform.linux;

//#include <tracy/TracyVulkan.hpp>

void* operator new(const std::size_t count)
{
    const auto ptr = malloc(count);
    TracyAlloc(ptr, count);
    return ptr;
}
void operator delete(void* ptr) noexcept
{
    TracyFree(ptr);
    free(ptr);
}


class LinuxContext
{
    ce::app::AppBase app;
    bool initialized = false;
    bool headless = false;
public:
    bool create(const std::vector<std::string>& args) noexcept
    {
        const bool server_mode = std::ranges::contains(args, "server");
        headless = std::ranges::contains(args, "headless");
        if (headless)
        {
            std::println("Starting headless server");
            app.init(false, server_mode, true);
            initialized = true;
            return true;
        }

        std::println("only headless mode is supported");
        return false;
    }
    void destroy()
    {

    }
    void main_loop()
    {
        std::println("starting main loop");
        static auto start_time = std::chrono::high_resolution_clock::now();
        while (true)
        {
            if (initialized)
            {
                const auto current_time = std::chrono::high_resolution_clock::now();
                const float delta_time = std::chrono::duration<float>(current_time - start_time).count();
                start_time = current_time;
                app.tick(delta_time, {});
            }
        }
    }
};

int main(const int argc, const char** argv)
{
    std::vector<std::string> args;
    args.reserve(argc);
    for (int i = 0; i < argc; ++i)
        args.emplace_back(argv[i]);
    LinuxContext context;
    context.create(args);
    context.main_loop();
    return 0;
}
