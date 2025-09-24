module;
#include <string>
#include <utility>
#include <vector>
#include <array>
#include <format>
#include <functional>
#include <string_view>
#include <span>
#include <ranges>

#include <volk.h>
#include <vk_mem_alloc.h>

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#include <vulkan/vulkan_android.h>
#elifdef _WIN32
#define LOGE(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
// #include <vulkan/vulkan_win32.h>
#endif

export module ce.vk.shader;
import ce.vk;
import ce.vk.utils;
import ce.platform.globals;

export namespace ce::vk
{
class ShaderModule
{
protected:
    Context& m_vk;
    std::string m_name;
    VkShaderModule m_module_vs = VK_NULL_HANDLE;
    VkShaderModule m_module_ps = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    ShaderModule(const std::shared_ptr<Context>& vk, std::string name) noexcept
        : m_vk(*vk), m_name(std::move(name)) { }
    [[nodiscard]] std::optional<VkShaderModule> create_shader_module(const std::string& asset_path) const noexcept
    {
        LOGI("Loading shader %s", asset_path.c_str());
        const auto& p = platform::GetPlatform();
        std::vector<uint8_t> shader_code;
        if (auto result = p.read_file(asset_path))
        {
            shader_code = std::move(result.value());
        }
        else
        {
            LOGE("Failed to read shader code: %s", asset_path.c_str());
            return std::nullopt;
        }
        assert(shader_code.size() % sizeof(uint32_t) == 0 && "shader bytecode should be aligned to 4 bytes");
        const VkShaderModuleCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = static_cast<uint32_t>(shader_code.size()),
            .pCode = reinterpret_cast<uint32_t*>(shader_code.data())
        };
        VkShaderModule module{VK_NULL_HANDLE};
        if (const VkResult result = vkCreateShaderModule(m_vk.device(), &info, nullptr, &module);
            result != VK_SUCCESS)
        {
            LOGE("Failed to create shader module: %s", utils::to_string(result));
            return std::nullopt;
        }
        const auto filename = [asset_path]
        {
            if (const auto pos = asset_path.find_last_of('/');
                pos != std::string_view::npos && pos < asset_path.size() - 1)
            {
                return asset_path.substr(pos + 1);
            }
            return asset_path;
        }();
        m_vk.debug_name(filename, module);
        return module;
    }
    bool load_from_file(const std::string& vs_path, const std::string& ps_path) noexcept
    {
        if (auto ps = create_shader_module(ps_path), vs = create_shader_module(vs_path); vs && ps)
        {
            m_module_ps = ps.value();
            m_module_vs = vs.value();
        }
        else
        {
            LOGE("Failed loading shader %s", m_name.c_str());
            return false;
        }
        return true;
    }
public:
    virtual ~ShaderModule() noexcept
    {
        LOGI("Destroying shader module: %s", m_name.c_str());
        if (m_module_vs != VK_NULL_HANDLE)
            vkDestroyShaderModule(m_vk.device(), m_module_vs, nullptr);
        if (m_module_ps != VK_NULL_HANDLE)
            vkDestroyShaderModule(m_vk.device(), m_module_ps, nullptr);
        m_module_vs = VK_NULL_HANDLE;
        m_module_ps = VK_NULL_HANDLE;
    }
};
}
