module;
#include <memory>
#include <volk.h>

export module ce.app:shaders;
import ce.vk;
import ce.vk.shader;
import ce.shaders.solidcolor;
import ce.shaders.solidflat;
import ce.shaders.textured;

export namespace ce::shaders
{
std::shared_ptr<SolidFlatShader> shader_opaque;
std::shared_ptr<SolidFlatShader> shader_transparent;
std::shared_ptr<SolidColorShader> shader_color;
std::shared_ptr<TexturedShader> shader_textured;

struct ShadersCreateInfo
{
    std::shared_ptr<vk::Context> vk;
    VkRenderPass renderpass = VK_NULL_HANDLE;
    uint32_t swapchain_count;
    VkSampleCountFlagBits sample_count = VK_SAMPLE_COUNT_1_BIT;
};
bool create_shaders(const ShadersCreateInfo& info) noexcept
{
    shader_opaque = std::make_shared<SolidFlatShader>(info.vk, "Opaque");
    shader_opaque->create(info.renderpass, info.swapchain_count, info.sample_count, 1, 100, false, false);
    shader_transparent = std::make_shared<SolidFlatShader>(info.vk, "Transparent");
    shader_transparent->create(info.renderpass, info.swapchain_count, info.sample_count, 1, 100, true, true);
    shader_color = std::make_shared<SolidColorShader>(info.vk, "Color");
    shader_color->create(info.renderpass, info.swapchain_count, info.sample_count, 1, 100, true, true, true);
    shader_textured = std::make_shared<TexturedShader>(info.vk, "Textured");
    shader_textured->create(info.renderpass, info.swapchain_count, info.sample_count, 1, 100, 100, false, false, false);
    return true;
}
void destroy_shaders() noexcept
{
    shader_opaque.reset();
    shader_transparent.reset();
    shader_color.reset();
    shader_textured.reset();
}
void reset_descriptors(uint32_t present_index) noexcept
{
    shader_opaque->reset_descriptors(present_index);
    shader_transparent->reset_descriptors(present_index);
    shader_color->reset_descriptors(present_index);
    shader_textured->reset_descriptors(present_index);
}
void update_descriptors() noexcept
{
    shader_opaque->update_descriptors();
    shader_transparent->update_descriptors();
    shader_color->update_descriptors();
    shader_textured->update_descriptors();
}
}
