module;
#include <optional>
#include <span>

#include <volk.h>
#include <vk_mem_alloc.h>
#include <stb_image.h>

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#else
#define LOGE(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

export module ce.vk.texture;
import ce.vk;
import ce.vk.utils;
import ce.vk.buffer;
import ce.platform;
import ce.platform.globals;
import glm;

export namespace ce::vk::texture
{
struct Texture
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info{};
    VkImageView image_view = VK_NULL_HANDLE;
    glm::uvec2 size{};
};
[[nodiscard]] std::optional<Texture> load_texture(const std::shared_ptr<Context>& vk, const std::string& path,
    Buffer& staging_buffer)
{
    const auto& p = platform::GetPlatform();
    const auto file_content = p.read_file(path);
    if (!file_content)
    {
        LOGE("Failed to read shader code: %s", path.c_str());
        return std::nullopt;
    }

    int32_t width, height, channels;
    if (const uint8_t* rgb = stbi_load_from_memory(file_content->data(), file_content->size(),
        &width, &height, &channels, STBI_rgb_alpha); rgb)
    {
        const VkImageCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_SRGB,
            .extent = VkExtent3D(width, height, 1),
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        VkImage image = VK_NULL_HANDLE;
        constexpr VmaAllocationCreateInfo vma_create_info{
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        };
        VmaAllocation allocation = VK_NULL_HANDLE;
        VmaAllocationInfo allocation_info{};
        if (const VkResult r = vmaCreateImage(vk->vma(), &create_info, &vma_create_info, &image, &allocation,
            &allocation_info); r != VK_SUCCESS)
        {
            LOGE("Could not create image. Error: %s", vk::utils::to_string(r));
            return std::nullopt;
        }
        const VkImageViewCreateInfo image_view_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = create_info.format,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
        };
        VkImageView image_view = VK_NULL_HANDLE;
        if (const VkResult r = vkCreateImageView(vk->device(), &image_view_info, nullptr, &image_view);
            r != VK_SUCCESS)
        {
            LOGE("Could not create image view. Error: %s", vk::utils::to_string(r));
            return std::nullopt;
        }

        if (const auto sb = staging_buffer.suballoc(width * height * 4, 64); sb)
        {
            const std::span src(rgb, width * height * 4);
            std::ranges::copy(src, static_cast<uint8_t*>(sb->ptr));
            vk->exec_immediate("init texture", [&](VkCommandBuffer cmd)
            {
                const VkImageMemoryBarrier barrier_transfer{
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .srcAccessMask = 0,
                    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .image = image,
                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
                };
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier_transfer);

                const VkBufferImageCopy copy_info{
                    .bufferOffset = sb->offset,
                    .bufferRowLength = static_cast<uint32_t>(width),
                    .bufferImageHeight = static_cast<uint32_t>(height),
                    .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                    .imageOffset = VkOffset3D{0, 0, 0},
                    .imageExtent = VkExtent3D{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1},
                };
                vkCmdCopyBufferToImage(cmd, staging_buffer.buffer(), image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_info);

                const VkImageMemoryBarrier barrier_sampling{
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .image = image,
                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
                };
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier_sampling);
            });
            staging_buffer.subfree(*sb);
            LOGI("Image Loaded");
            return Texture{
                .image = image,
                .allocation = allocation,
                .allocation_info = allocation_info,
                .image_view = image_view,
                .size = {width, height},
            };
        }
    }
    return std::nullopt;
}
[[nodiscard]] std::optional<VkSampler> create_sampler(const std::shared_ptr<vk::Context>& vk) noexcept
{
    const VkSamplerCreateInfo sampler_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.f,
        .anisotropyEnable = false,
        .maxAnisotropy = 1.f,
        .compareEnable = false,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.f,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = false,
    };
    VkSampler sampler = VK_NULL_HANDLE;
    if (const VkResult r = vkCreateSampler(vk->device(), &sampler_info, nullptr, &sampler); r != VK_SUCCESS)
    {
        LOGE("Failed to create sampler");
        return std::nullopt;
    }
    return sampler;
}
}