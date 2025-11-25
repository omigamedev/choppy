module;
#include <optional>
#include <span>
#include <string>
#include <memory>
#include <algorithm>
#include <vector>
#include <format>

#include <volk.h>
#include <vk_mem_alloc.h>
#include <stb_image.h>
#include <stb_image_write.h>

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
[[nodiscard]] std::optional<Texture> load_texture(const std::shared_ptr<Context>& vk,
    const std::string& path, Buffer& staging_buffer) noexcept
{
    const auto& p = platform::GetPlatform();
    const auto file_content = p.read_file(path);
    if (!file_content)
    {
        LOGE("Failed to read texture: %s", path.c_str());
        return std::nullopt;
    }

    int32_t width, height, channels;
    const auto rgb = std::unique_ptr<uint8_t[]>(stbi_load_from_memory(file_content->data(),
        static_cast<int32_t>(file_content->size()), &width, &height, &channels, STBI_rgb_alpha));
    if (!rgb)
    {
        LOGE("failed to open texture: %s", path.c_str());
        return std::nullopt;
    }
    const uint32_t mip_levels = glm::gtx::log2(std::min(width, height));
    const VkImageCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .extent = VkExtent3D(width, height, 1),
        .mipLevels = mip_levels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
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
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, 1}
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
        const std::span src(rgb.get(), width * height * 4);
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
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, 1}
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

            int32_t current_width = width;
            int32_t current_height = height;
            // mip <= mip_levels assures that all the mips have the same layout
            for (uint32_t mip = 1; mip <= mip_levels; ++mip)
            {
                const VkImageMemoryBarrier barrier_src{
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    .image = image,
                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip-1, 1, 0, 1}
                };
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier_src);

                // don't copy the last+1 mip
                if (mip < mip_levels)
                {
                    const VkImageBlit blit_region{
                        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip-1, 0, 1},
                        .srcOffsets = {VkOffset3D{0, 0, 0}, VkOffset3D{current_width, current_height, 1}},
                        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1},
                        .dstOffsets = {VkOffset3D{0, 0, 0}, VkOffset3D{current_width / 2, current_height / 2, 1}},
                    };
                    vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit_region, VK_FILTER_NEAREST);
                }
                current_width /= 2;
                current_height /= 2;
            }

            const VkImageMemoryBarrier barrier_sampling{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .image = image,
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, 1}
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
    return std::nullopt;
}
[[nodiscard]] std::optional<Texture> load_atlas(const std::shared_ptr<Context>& vk,
    const std::string& path, Buffer& staging_buffer, const glm::uvec2& grid_size) noexcept
{
    const auto& p = platform::GetPlatform();
    const auto file_content = p.read_file(path);
    if (!file_content)
    {
        LOGE("Failed to read texture: %s", path.c_str());
        return std::nullopt;
    }

    int32_t width, height, channels;
    const auto rgb = std::unique_ptr<uint8_t[]>(stbi_load_from_memory(file_content->data(),
        static_cast<int32_t>(file_content->size()), &width, &height, &channels, STBI_rgb_alpha));
    if (!rgb)
    {
        LOGE("failed to open texture: %s", path.c_str());
        return std::nullopt;
    }

    // // split the grid
    // // NOTE: check if texture size is divisible by grid size
    const glm::uvec2 cell_size = glm::uvec2(width, height) / grid_size;
    // std::vector<std::vector<uint8_t>> cells;
    // for (uint32_t row = 0; row < grid_size.y; ++row)
    // {
    //     for (uint32_t col = 0; col < grid_size.x; ++col)
    //     {
    //         auto& cell = cells.emplace_back();
    //         cell.resize(cell_size.x * cell_size.y * channels);
    //         for (uint32_t y = 0; y < cell_size.y; ++y)
    //         {
    //             const uint8_t* src_line = rgb.get() +
    //                 (row * width * cell_size.y + col * cell_size.x + y * width) * channels;
    //             uint8_t* dst_line = cell.data() + (y * cell_size.x) * channels;
    //             std::copy_n(src_line, cell_size.x * channels, dst_line);
    //         }
    //         std::string name = std::format("cell{}{}.png", row, col);
    //         stbi_write_png(name.c_str(), cell_size.x, cell_size.y, channels, cell.data(), 0);
    //     }
    // }

    const uint32_t mip_levels = glm::gtx::log2(std::min(cell_size.x, cell_size.y));
    const uint32_t layers = grid_size.x * grid_size.y;
    const VkImageCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .extent = VkExtent3D(cell_size.x, cell_size.y, 1),
        .mipLevels = mip_levels,
        .arrayLayers = layers,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
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
        .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        .format = create_info.format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, layers}
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
        const std::span src(rgb.get(), width * height * 4);
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
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, layers}
            };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier_transfer);

            std::vector<VkBufferImageCopy> copy_cells;
            copy_cells.reserve(layers);
            for (uint32_t row = 0; row < grid_size.y; ++row)
            {
                for (uint32_t col = 0; col < grid_size.x; ++col)
                {
                    const uint32_t layer_index = row * grid_size.x + col;
                    const VkDeviceSize src_line = (row * width * cell_size.y + col * cell_size.x) * channels;
                    copy_cells.emplace_back(VkBufferImageCopy{
                        .bufferOffset = sb->offset + src_line,
                        .bufferRowLength = static_cast<uint32_t>(width),
                        .bufferImageHeight = static_cast<uint32_t>(height),
                        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer_index, 1},
                        .imageOffset = VkOffset3D{0, 0, 0},
                        .imageExtent = VkExtent3D{cell_size.x, cell_size.y, 1},
                    });
                }
            }
            vkCmdCopyBufferToImage(cmd, staging_buffer.buffer(), image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copy_cells.size(), copy_cells.data());

            int32_t current_width = cell_size.x;
            int32_t current_height = cell_size.y;
            // mip <= mip_levels assures that all the mips have the same layout
            for (uint32_t mip = 1; mip <= mip_levels; ++mip)
            {
                std::vector<VkImageMemoryBarrier> barriers;
                barriers.reserve(layers);
                for (uint32_t layer = 0; layer < layers; ++layer)
                {
                    barriers.emplace_back(VkImageMemoryBarrier{
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        .image = image,
                        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip-1, 1, layer, 1}
                    });
                }
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, barriers.size(), barriers.data());

                // don't copy the last+1 mip
                if (mip < mip_levels)
                {
                    std::vector<VkImageBlit> regions;
                    regions.reserve(layers);
                    for (uint32_t layer = 0; layer < layers; ++layer)
                    {
                        regions.emplace_back(VkImageBlit{
                            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip-1, layer, 1},
                            .srcOffsets = {VkOffset3D{0, 0, 0}, VkOffset3D{current_width, current_height, 1}},
                            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1},
                            .dstOffsets = {VkOffset3D{0, 0, 0}, VkOffset3D{current_width / 2, current_height / 2, 1}},
                        });
                    }
                    vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regions.size(), regions.data(), VK_FILTER_LINEAR);
                }
                current_width /= 2;
                current_height /= 2;
            }

            const VkImageMemoryBarrier barrier_sampling{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .image = image,
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, layers}
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
    return std::nullopt;
}
[[nodiscard]] std::optional<VkSampler> create_sampler(const std::shared_ptr<vk::Context>& vk) noexcept
{
    const VkSamplerCreateInfo sampler_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
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