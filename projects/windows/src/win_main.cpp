#include <memory>
import ce.platform_windows;
import ce.xr;
import ce.vk;

int main()
{
    auto platform = std::make_unique<ce::platform::PlatformWindows>();
    ce::xr::Instance xr_instance;
    xr_instance.create();
    ce::vulkan::Context vk_context;
    vk_context.create_instance(xr_instance.vulkan_version(), xr_instance.instance_extensions());
    auto xr_physical_device = xr_instance.physical_device(vk_context.instance());
    vk_context.create_device(xr_physical_device, xr_instance.device_extensions());
    return 0;
}
