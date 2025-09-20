module;
#include <array>
#include <span>
#include <vector>
#include <memory>
#include <ranges>
#include <functional>
#include <thread>
#include <map>
#include <mutex>
#include <volk.h>
#include <PerlinNoise.hpp>

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#include "vk_mem_alloc.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "tracy/TracyC.h"

#ifdef __ANDROID__
#include <jni.h>
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#elifdef _WIN32
#include <windows.h>
#define LOGE(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

export module ce.app;
import ce.platform;
import ce.platform.globals;
import ce.xr;
import ce.vk;
import ce.vk.utils;
import ce.shaders.solidflat;
import ce.app.grid;
import ce.app.cube;
import ce.app.utils;
import ce.app.chunkgen;
import ce.app.chunkmesh;
import glm;

export namespace ce::app
{
enum class GamepadButton : uint8_t
{
    Menu, View,
    A, B, X, Y,
    Up, Down, Left, Right,
    ShoulderLeft, ShoulderRight,
    ThumbLeft, ThumbRight,
    EnumCount
};

bool operator&(GamepadButton lhs, GamepadButton rhs)
{
    return static_cast<uint8_t>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
}

struct GamepadState
{
    std::array<bool, static_cast<size_t>(GamepadButton::EnumCount)> buttons{false};
    float thumbstick_left[2]{};
    float thumbstick_right[2]{};
    float trigger_left{0};
    float trigger_right{0};
};
struct PlayerState
{
    bool dragging = false;
    glm::ivec2 drag_start = { 0, 0 };
    float cam_fov = 90.f;
    glm::vec2 cam_start = {0, 0};
    glm::vec2 cam_angles = { 0, 0 };
    glm::vec3 cam_pos = { 0, 0, 0 };
    glm::ivec3 cam_sector = { 0, 0, 0 };
    std::array<bool, 256> keys{false};
};
struct ChunkUpdate : NoCopy
{
    std::map<BlockType, ChunkMesh<shaders::SolidFlatShader::VertexInput>> data;
    size_t chunk_index = 0;
    glm::vec4 color{};
    glm::ivec3 sector{};
};
struct Texture
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_info{};
    VkImageView image_view = VK_NULL_HANDLE;
    glm::uvec2 size{};
};
struct ChunksState
{
    //vk::BufferSuballocation vertex_buffer{};
    vk::BufferSuballocation uniform_buffer{};
    vk::BufferSuballocation args_buffer{};
};
class AppBase final
{
    std::shared_ptr<xr::Context> m_xr;
    std::shared_ptr<vk::Context> m_vk;
    std::shared_ptr<shaders::SolidFlatShader> solid_flat;
    shaders::SolidFlatShader::PerFrameConstants uniform{};
	siv::PerlinNoise perlin{ std::random_device{} };
    //std::shared_ptr<Grid> m_grid;
    std::vector<Chunk> m_chunks;
    VkRenderPass m_renderpass = VK_NULL_HANDLE;
    VkSampleCountFlagBits m_sample_count = VK_SAMPLE_COUNT_1_BIT;
    glm::ivec2 m_size{0, 0};

    std::multimap<uint64_t, std::pair<vk::Buffer&, const vk::BufferSuballocation>> m_delete_buffers;
    std::vector<std::tuple<vk::Buffer&, const vk::BufferSuballocation, VkDeviceSize /*dst_offset*/>> m_copy_buffers;
    vk::Buffer m_staging_buffer;
    vk::Buffer m_vertex_buffer;
    vk::Buffer m_frame_buffer;
    vk::Buffer m_object_buffer;
    VkDescriptorSet m_frame_descriptor_set = VK_NULL_HANDLE;
    VkDescriptorSet m_object_descriptor_set = VK_NULL_HANDLE;

    Texture m_texture;
    VkSampler m_sampler = VK_NULL_HANDLE;

    vk::Buffer m_args_buffer;
    uint32_t m_draw_count = 0;
    bool m_running = true;
    PlayerState m_player{};
    bool xrmode = false;
    uint32_t m_swapchain_count = 0;
    uint64_t m_timeline_value = 0;

    // Size of a block in meters
    static constexpr float BlockSize = 0.5f;
    // Number of blocks per chunk
    static constexpr uint32_t ChunkSize = 32;
    static constexpr uint32_t ChunkRings = 5;

    FlatGenerator generator{ChunkSize, 20};
    SimpleMesher<shaders::SolidFlatShader::VertexInput> mesher{};
    // std::vector<uint32_t> m_chunk_updates;
    TracyLockable(std::mutex, m_chunks_mutex);
    std::thread m_chunks_thread;
    ChunksState m_chunks_state;
    bool needs_update = false;

public:
    ~AppBase()
    {
        m_running = false;
        if (m_vk && m_vk->device())
        {
            vkDeviceWaitIdle(m_vk->device());
        }
        for (auto& c : m_chunks)
        {
            if (!c.dirty && c.buffer.alloc)
                m_vertex_buffer.subfree(c.buffer);
        }
        if (m_chunks_thread.joinable())
            m_chunks_thread.join();
        clear_chunks_state(0);
        garbage_collect(true);
    };
    [[nodiscard]] auto& xr() noexcept { return m_xr; }
    [[nodiscard]] auto& vk() noexcept { return m_vk; }

    [[nodiscard]] std::optional<Texture> load_texture(const std::string& path)
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
                .format = VK_FORMAT_R8G8B8A8_UNORM,
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
            if (const VkResult r = vmaCreateImage(m_vk->vma(), &create_info, &vma_create_info, &image, &allocation,
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
            if (const VkResult r = vkCreateImageView(m_vk->device(), &image_view_info, nullptr, &image_view);
                r != VK_SUCCESS)
            {
                LOGE("Could not create image view. Error: %s", vk::utils::to_string(r));
                return std::nullopt;
            }

            if (const auto sb = m_staging_buffer.suballoc(width * height * 4, 64); sb)
            {
                const std::span src(rgb, width * height * 4);
                std::ranges::copy(src, static_cast<uint8_t*>(sb->ptr));
                m_vk->exec_immediate("init texture", [&](VkCommandBuffer cmd)
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
                    vkCmdCopyBufferToImage(cmd, m_staging_buffer.buffer(), image,
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
                m_staging_buffer.subfree(*sb);
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

    void init(const bool xr_mode) noexcept
    {
        xrmode = xr_mode;

        if (xrmode)
        {
            m_renderpass = m_xr->renderpass();
            m_sample_count = m_xr->sample_count();
            m_swapchain_count = m_xr->swapchain_count();
        }
        else
        {
            m_renderpass = m_vk->renderpass();
            m_sample_count = m_vk->samples_count();
            m_swapchain_count = m_vk->swapchain_count();
        }

        solid_flat = std::make_shared<shaders::SolidFlatShader>(m_vk, "Test");
        solid_flat->create(m_renderpass, m_swapchain_count, m_sample_count, 1, 1);

        // Create the grid object
        //m_grid = std::make_shared<Grid>();
        //m_grid->create(m_vk, 0);

        // Create and upload vertex buffer
        m_vertex_buffer = vk::Buffer(m_vk, "ChunksVertexBuffer");
        if (!m_vertex_buffer.create(1024 * (1 << 20),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE))
        {
            LOGE("Failed to create chunks vertex buffer");
        }

        m_frame_buffer = vk::Buffer(m_vk, "ChunksPerFrameBuffer");
        if (!m_frame_buffer.create(sizeof(shaders::SolidFlatShader::PerFrameConstants) * m_swapchain_count * 2,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE))
        {
            LOGE("Failed to create cube vertex buffer");
        }

        m_object_buffer = vk::Buffer(m_vk, "ChunksPerObjectBuffer");
        if (!m_object_buffer.create(sizeof(shaders::SolidFlatShader::PerObjectBuffer) * 30'000,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE))
        {
            LOGE("Failed to create cube vertex buffer");
        }

        m_staging_buffer = vk::Buffer(m_vk, "StagingBuffer");
        if (!m_staging_buffer.create(128 * (1 << 20),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
            VMA_ALLOCATION_CREATE_MAPPED_BIT))
        {
            LOGE("Failed to create cube vertex buffer");
        }

        m_args_buffer = vk::Buffer(m_vk, "CubeArgsBuffer");
        constexpr uint32_t args_buffer_size = sizeof(VkDrawIndirectCommand) * 30'000;
        if (!m_args_buffer.create(args_buffer_size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE))
        {
            LOGE("Failed to create cube vertex buffer");
        }

        m_vk->exec_immediate("init resources", [this](VkCommandBuffer cmd){
            if (xrmode)
            {
                m_xr->init_resources(cmd);
            }
            else
            {
                m_vk->init_resources(cmd);
            }
        });

        m_texture = load_texture("assets/grass.png").value();

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
        if (const VkResult r = vkCreateSampler(m_vk->device(), &sampler_info, nullptr, &m_sampler); r != VK_SUCCESS)
        {
            LOGE("Failed to create sampler");
        }
        m_chunks_thread = std::thread(&AppBase::generate_thread, this);
    }
    void generate_thread() noexcept
    {
        tracy::SetThreadName("generate_thread");
        while (m_running)
        {
            needs_update |= generate_chunks();
        }
    }
    [[nodiscard]] std::vector<glm::ivec3> generate_neighbors(const glm::ivec3 origin, const int32_t size) const noexcept
    {
        ZoneScoped;
        std::vector<glm::ivec3> neighbors;
        const uint32_t chunk_count = pow(size * 2 + 1, 3);
        neighbors.reserve(chunk_count);
        const int32_t side = size;
        for (int32_t y = -1; y < 1 + 1; ++y)
        {
            for (int32_t z = -side; z < side + 1; ++z)
            {
                for (int32_t x = -side; x < side + 1; ++x)
                {
                    const glm::ivec3 sector = glm::ivec3{x, y, z} + glm::ivec3(origin.x, 0, origin.z);
                    neighbors.emplace_back(sector);
                }
            }
        }
        return neighbors;
    }
    [[nodiscard]] bool generate_chunks() noexcept
    {
        ZoneScoped;
        constexpr uint32_t chunk_count = (ChunkRings * 2 + 1) * (ChunkRings * 2 + 1) * (3);

        const glm::vec3 cur_sector = m_player.cam_pos / (ChunkSize * BlockSize);
        //const glm::vec3 dist = glm::abs(cur_sector - glm::vec3(m_player.cam_sector));
        if (m_chunks.size() < chunk_count || glm::any(glm::notEqual(m_player.cam_sector, glm::ivec3(glm::floor(cur_sector)))))// || dist.x > .6 || dist.y > .6 || dist.z > .6)
        {
            m_player.cam_sector = glm::floor(cur_sector);
            LOGI("travel to sector [%d, %d, %d]", m_player.cam_sector.x, m_player.cam_sector.y, m_player.cam_sector.z);
        }
        //else
        //{
        //    return false;
        //}

        const auto neighbors = generate_neighbors(m_player.cam_sector, ChunkRings);

        std::vector<size_t> chunk_indices;
        chunk_indices.reserve(chunk_count);
        for (size_t i = 0; i < m_chunks.size(); ++i)
        {
            if (auto it = std::ranges::find(neighbors, m_chunks[i].sector); it == neighbors.end())
            {
                chunk_indices.emplace_back(i);
            }
        }

        size_t chunk_indices_offset = 0;
        for (const auto& sector : neighbors)
        {
            // check if it's already present
            if (auto it = std::ranges::find(m_chunks, sector,
                [](auto& p){ return p.sector; }); it != m_chunks.end())
            {
                continue;
            }
            if (m_chunks.size() < chunk_count)
            {
                const auto blocks_data = generator.generate(sector);
                auto chunk_data = mesher.mesh(blocks_data, BlockSize);
                std::lock_guard lock(m_chunks_mutex);
                // m_chunk_updates.emplace_back(m_chunks.size());
                auto& chunk = m_chunks.emplace_back();
                chunk.mesh = std::move(chunk_data);
                chunk.color = glm::gtc::linearRand(glm::vec4(0, 0, 0, 1), glm::vec4(1, 1, 1, 1));
                chunk.sector = sector;
                chunk.transform = glm::gtc::translate(glm::vec3(sector) * ChunkSize * BlockSize);
                chunk.dirty = true;
                break;
            }
            else if (!chunk_indices.empty())
            {
                const auto blocks_data = generator.generate(sector);
                auto chunk_data = mesher.mesh(blocks_data, BlockSize);
                const uint32_t chunk_index = chunk_indices[chunk_indices_offset++];
                std::lock_guard lock(m_chunks_mutex);
                auto& chunk = m_chunks[chunk_index];
                chunk.mesh = std::move(chunk_data);
                chunk.color = glm::gtc::linearRand(glm::vec4(0, 0, 0, 1), glm::vec4(1, 1, 1, 1));
                chunk.sector = sector;
                chunk.transform = glm::gtc::translate(glm::vec3(sector) * ChunkSize * BlockSize);
                chunk.dirty = true;
                break;
            }
        }
        return true;
    }
    void clear_chunks_state(const uint64_t timeline_value) noexcept
    {
        ZoneScoped;
        //if (m_chunks_state.vertex_buffer.alloc)
        //    m_delete_buffers.emplace(timeline_value, std::pair(std::ref(m_vertex_buffer), m_chunks_state.vertex_buffer));
        if (m_chunks_state.uniform_buffer.alloc)
            m_delete_buffers.emplace(timeline_value, std::pair(std::ref(m_object_buffer), m_chunks_state.uniform_buffer));
        if (m_chunks_state.args_buffer.alloc)
            m_delete_buffers.emplace(timeline_value, std::pair(std::ref(m_args_buffer), m_chunks_state.args_buffer));
    }
    void update_chunks(const vk::utils::FrameContext& frame) noexcept
    {
        ZoneScoped;

        clear_chunks_state(frame.timeline_value);
        // auto sorted_chunks = sorted_view(m_chunks, [this](const auto& c1, const auto& c2) -> bool
        // {
        //     const float dist1 = glm::gtx::distance2(glm::vec3(c1.sector), glm::vec3(m_player.cam_pos));
        //     const float dist2 = glm::gtx::distance2(glm::vec3(c2.sector), glm::vec3(m_player.cam_pos));
        //     return dist1 >= dist2;
        // });

        // std::ranges::sort(m_chunks, [this](const auto& c1, const auto& c2) -> bool
        // {
        //     const float dist1 = glm::gtx::distance2(glm::vec3(c1.sector), glm::vec3(m_player.cam_pos));
        //     const float dist2 = glm::gtx::distance2(glm::vec3(c2.sector), glm::vec3(m_player.cam_pos));
        //     return dist1 > dist2;
        // });

        //std::ranges::shuffle(m_chunks, std::mt19937{std::random_device{}()});

        std::vector<shaders::SolidFlatShader::PerObjectBuffer> ubo;
        ubo.reserve(m_chunks.size());
        std::vector<VkDrawIndirectCommand> draw_args;
        draw_args.reserve(m_chunks.size());
        m_draw_count = 0;
        uint32_t polys = 0;
        std::lock_guard lock(m_chunks_mutex);
        for (auto& chunk : m_chunks)
        {
            std::vector<shaders::SolidFlatShader::VertexInput> vbo;
            for (const auto& [k, m] : chunk.mesh)
            {
                vbo.append_range(m.vertices);
                polys += m.vertices.size();
            }
            if (vbo.empty())
                continue;

            if (chunk.dirty)
            {
                if (const auto sb = m_staging_buffer.suballoc(vbo.size() *
                    sizeof(shaders::SolidFlatShader::VertexInput), 64))
                {
                    if (const auto dst_sb = m_vertex_buffer.suballoc(sb->size, 64))
                    {
                        std::ranges::copy(vbo, static_cast<shaders::SolidFlatShader::VertexInput*>(sb->ptr));
                        m_copy_buffers.emplace_back(m_vertex_buffer, *sb, dst_sb->offset);
                        //m_chunks_state.vertex_buffer = *dst_sb;

                        if (chunk.buffer.alloc)
                            m_delete_buffers.emplace(frame.timeline_value, std::pair(std::ref(m_vertex_buffer), chunk.buffer));
                        chunk.buffer = *dst_sb;
                        chunk.dirty = false;
                    }
                    // defer suballocation deletion
                    m_delete_buffers.emplace(frame.timeline_value, std::pair(std::ref(m_staging_buffer), *sb));
                }
            }

            m_draw_count++;

            ubo.push_back({
                .ObjectTransform = glm::transpose(chunk.transform),
                .ObjectColor = glm::vec4(glm::vec3(chunk.color), 1.0f),
                .selected = false,
            });
            const auto vertex_offset = chunk.buffer.offset / sizeof(shaders::SolidFlatShader::VertexInput);
            draw_args.push_back({
                .vertexCount = static_cast<uint32_t>(vbo.size()),
                .instanceCount = 1,
                .firstVertex = static_cast<uint32_t>(vertex_offset),
                .firstInstance = 0
            });
        }
        LOGI("drawing %d polys", polys / 3);
        if (m_draw_count > 0)
        {
            if (const auto sb = m_staging_buffer.suballoc(ubo.size() *
                sizeof(shaders::SolidFlatShader::PerObjectBuffer), 64))
            {
                if (const auto dst_sb = m_object_buffer.suballoc(sb->size, 64))
                {
                    std::ranges::copy(ubo, static_cast<shaders::SolidFlatShader::PerObjectBuffer*>(sb->ptr));
                    m_copy_buffers.emplace_back(m_object_buffer, *sb, dst_sb->offset);
                    m_chunks_state.uniform_buffer = *dst_sb;
                }
                // defer suballocation deletion
                m_delete_buffers.emplace(frame.timeline_value, std::pair(std::ref(m_staging_buffer), *sb));
            }

            if (const auto sb = m_staging_buffer.suballoc(draw_args.size() * sizeof(VkDrawIndirectCommand), 64))
            {
                if (const auto dst_sb = m_args_buffer.suballoc(sb->size, 64))
                {
                    std::ranges::copy(draw_args, static_cast<VkDrawIndirectCommand*>(sb->ptr));
                    m_copy_buffers.emplace_back(m_args_buffer, *sb, dst_sb->offset);
                    m_chunks_state.args_buffer = *dst_sb;
                }
                // defer suballocation deletion
                m_delete_buffers.emplace(frame.timeline_value, std::pair(std::ref(m_staging_buffer), *sb));
            }
        }
    }
    void update(const vk::utils::FrameContext& frame) noexcept
    {
        ZoneScoped;
        solid_flat->reset_descriptors(frame.present_index);

        if (needs_update)
        {
            update_chunks(frame);
            needs_update = false;
        }

        const auto per_frame_constants = shaders::SolidFlatShader::PerFrameConstants{
            .ViewProjection = {
                glm::transpose(frame.projection[0] * frame.view[0]),
                glm::transpose(frame.projection[1] * frame.view[1]),
            }
        };

        if (const auto sb = m_staging_buffer.suballoc(sizeof(per_frame_constants), 64))
        {
            const auto dst_sb = m_frame_buffer.suballoc(sb->size, 64);
            std::copy_n(&per_frame_constants, 1, static_cast<shaders::SolidFlatShader::PerFrameConstants*>(sb->ptr));
            // defer copy
            m_copy_buffers.emplace_back(m_frame_buffer, *sb, dst_sb->offset);
            if (const auto set = solid_flat->alloc_descriptor(frame.present_index, 0))
            {
                m_frame_descriptor_set = *set;
                solid_flat->write_buffer(*set, 0, m_frame_buffer.buffer(),
                    dst_sb->offset, dst_sb->size, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
                solid_flat->write_texture(*set, 1, m_texture.image_view, m_sampler);
            }

            // defer suballocation deletion
            m_delete_buffers.emplace(frame.timeline_value, std::pair(std::ref(m_staging_buffer), *sb));
            m_delete_buffers.emplace(frame.timeline_value, std::pair(std::ref(m_frame_buffer), *dst_sb));
        }
        if (const auto set = solid_flat->alloc_descriptor(frame.present_index, 1))
        {
            m_object_descriptor_set = *set;
            solid_flat->write_buffer(*set, 0, m_object_buffer.buffer(),
                m_chunks_state.uniform_buffer.offset, m_chunks_state.uniform_buffer.size,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        solid_flat->update_descriptors();
    }
    void render(const vk::utils::FrameContext& frame, const float dt, VkCommandBuffer cmd) noexcept
    {
        ZoneScoped;
        {
            TracyVkZone(m_vk->tracy(), cmd, "Copy Buffers");
            for (auto& [buffer, sb, dst_offset] : m_copy_buffers)
            {
                buffer.copy_from(frame.cmd, m_staging_buffer.buffer(), sb, dst_offset);
            }
            m_copy_buffers.clear();
        }

        {
            TracyVkZone(m_vk->tracy(), cmd, "Copy Barrier");
            const VkBufferMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = m_args_buffer.buffer(),
                .offset = m_chunks_state.args_buffer.offset,
                .size = m_chunks_state.args_buffer.size,
            };

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                0, 0, nullptr, 1, &barrier, 0, nullptr);
        }

        char zone_name[64];
        snprintf(zone_name, sizeof(zone_name), "RenderPass %llu", (unsigned long long)frame.timeline_value);
        TracyVkZoneTransient(m_vk->tracy(), render_pass_zone, cmd, zone_name, true);

        // Renderpass setup
        constexpr std::array rgb{.3f, .3f, .3f};
        constexpr std::array clear_value{
            VkClearValue{.color = {rgb[0], rgb[1], rgb[2], 1.f}},
            VkClearValue{.depthStencil = {1.f, 0u}}
        };
        const std::array renderpass_views{frame.color_view, frame.depth_view, frame.resolve_color_view};
        const VkRenderPassAttachmentBeginInfo renderpass_attachment{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO,
            .attachmentCount = static_cast<uint32_t>(renderpass_views.size()),
            .pAttachments = renderpass_views.data()
        };
        const VkRenderPassBeginInfo renderpass_info{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = &renderpass_attachment,
            .renderPass = frame.renderpass,
            .framebuffer = frame.framebuffer,
            .renderArea = VkRect2D{.extent = frame.size},
            .clearValueCount = static_cast<uint32_t>(clear_value.size()),
            .pClearValues = clear_value.data(),
        };
        constexpr VkSubpassBeginInfo subpass_begin_info{
            .sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO,
            .contents = VK_SUBPASS_CONTENTS_INLINE,
        };
        vkCmdBeginRenderPass2(cmd, &renderpass_info, &subpass_begin_info);

        // Begin rendering

        if (xrmode)
        {
            const VkViewport viewport = m_xr->viewport();
            vkCmdSetViewportWithCount(cmd, 1, &viewport);
            const VkRect2D scissor = m_xr->scissor();
            vkCmdSetScissorWithCount(cmd, 1, &scissor);
        }
        else
        {
            const VkViewport viewport = m_vk->viewport();
            vkCmdSetViewportWithCount(cmd, 1, &viewport);
            const VkRect2D scissor = m_vk->scissor();
            vkCmdSetScissorWithCount(cmd, 1, &scissor);
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, solid_flat->pipeline());

        // {
        //     const std::array vertex_buffers{m_vertex_buffer->buffer()};
        //     constexpr std::array vertex_buffers_offset{VkDeviceSize{0}};
        //     vkCmdBindVertexBuffers(cmd, 0, vertex_buffers.size(), vertex_buffers.data(), vertex_buffers_offset.data());
        //     const VkDescriptorSet& descriptor_set = solid_flat->descriptor_set(0);
        //     vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        //         solid_flat->layout(), 0, 1, &descriptor_set, 0, nullptr);
        //     vkCmdDraw(cmd, vertices.size(), 1, 0, 0);
        // }

        // Drawing Grid
        // {
        //     const std::array vertex_buffers{m_grid->vertex_buffer()->buffer()};
        //     constexpr std::array vertex_buffers_offset{VkDeviceSize{0}};
        //     vkCmdBindVertexBuffers(cmd, 0, vertex_buffers.size(), vertex_buffers.data(), vertex_buffers_offset.data());
        //     vkCmdBindIndexBuffer(cmd, m_grid->index_buffer()->buffer(), 0, VK_INDEX_TYPE_UINT32);
        //
        //     const std::array<VkDescriptorSet, 2> sets = solid_flat->descriptor_sets(0);
        //     vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        //         solid_flat->layout(), 0, sets.size(), sets.data(), 0, nullptr);
        //
        //     // Draw the cube using its indices
        //     vkCmdDrawIndexed(cmd, m_grid->index_count(), 1, 0, 0, 0);
        // }

        // std::lock_guard lock(m_chunks_mutex);
        if (!m_chunks.empty())
        {
            const std::array vertex_buffers{m_vertex_buffer.buffer()};
            const std::array vertex_buffers_offset{VkDeviceSize{0ull}};
            vkCmdBindVertexBuffers(cmd, 0, static_cast<uint32_t>(vertex_buffers.size()),
                vertex_buffers.data(), vertex_buffers_offset.data());

            const std::array sets{m_frame_descriptor_set, m_object_descriptor_set};
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                solid_flat->layout(), 0, sets.size(), sets.data(), 0, nullptr);

            vkCmdDrawIndirect(cmd, m_args_buffer.buffer(),
                m_chunks_state.args_buffer.offset, m_draw_count, sizeof(VkDrawIndirectCommand));
        }

        /*
        if (xrmode)
        {
            // Bind the cube's vertex and index buffers
            for (uint32_t i = 0; i < 2; ++i)
            {
                const std::array vertex_buffers{m_cube->vertex_buffer()->buffer()};
                constexpr std::array vertex_buffers_offset{VkDeviceSize{0}};
                vkCmdBindVertexBuffers(cmd, 0, vertex_buffers.size(), vertex_buffers.data(), vertex_buffers_offset.data());
                vkCmdBindIndexBuffer(cmd, m_cube->index_buffer()->buffer(), 0, VK_INDEX_TYPE_UINT32);

                const VkDescriptorSet& descriptor_set = solid_flat->descriptor_set(1 + i);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    solid_flat->layout(), 0, 1, &descriptor_set, 0, nullptr);

                // Draw the cube using its indices
                vkCmdDrawIndexed(cmd, m_cube->index_count(), 1, 0, 0, 0);
            }
        }
        */

        // End rendering

        constexpr VkSubpassEndInfo subpass_end_info{.sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO};
        vkCmdEndRenderPass2(cmd, &subpass_end_info);

        TracyVkCollect(m_vk->tracy(), cmd);
    }
    glm::mat4 update_flying_camera_angles(const float dt, const GamepadState& gamepad, const xr::TouchControllerState& touch)
    {
        ZoneScoped;
        constexpr float dead_zone = 0.05;
        constexpr float steering_speed = 90.f;
        const glm::vec2 rthumb = glm::gtc::make_vec2(gamepad.thumbstick_right);
        const glm::vec2 abs_rthumb = glm::abs(rthumb);
        if (abs_rthumb.x > dead_zone)
        {
            m_player.cam_angles.x += glm::radians(rthumb.x * dt * steering_speed);
        }
        if (abs_rthumb.y > dead_zone)
        {
            m_player.cam_angles.y += glm::radians(-rthumb.y * dt * steering_speed);
        }

        const glm::vec2 touch_abs_rthumb = glm::abs(touch.thumbstick_right);
        if (touch_abs_rthumb.x > dead_zone)
        {
            m_player.cam_angles.x += glm::radians(touch.thumbstick_right.x * dt * steering_speed);
        }
        if (touch_abs_rthumb.y > dead_zone)
        {
            m_player.cam_angles.y -= glm::radians(-touch.thumbstick_right.y * dt * steering_speed);
        }
        return glm::gtx::eulerAngleYX(-m_player.cam_angles.x, -m_player.cam_angles.y);
    }
    void update_flying_camera_pos(const float dt, const GamepadState& gamepad,
        const xr::TouchControllerState& touch, const glm::mat4& view)
    {
        ZoneScoped;
        constexpr float dead_zone = 0.05;
#ifdef _WIN32
        float speed = 1.f;
        if (m_player.keys[VK_SHIFT])
            speed = speed * 0.25f;
        if (m_player.keys[VK_CONTROL])
            speed = speed * 4.f;
        speed = speed + 10.f * gamepad.trigger_right;
#else
        const float speed = 3.f + 10.f * touch.trigger_left;
#endif
        if (m_player.keys['W'])
        {
            const glm::vec4 forward = glm::vec4{0, 0, -1, 1} * view;
            m_player.cam_pos += glm::vec3(forward) * dt * speed;
        }
        if (m_player.keys['S'])
        {
            const glm::vec4 forward = glm::vec4{0, 0, 1, 1} * view;
            m_player.cam_pos += glm::vec3(forward) * dt * speed;
        }
        if (m_player.keys['A'])
        {
            const glm::vec4 forward = glm::vec4{-1, 0, 0, 1} * view;
            m_player.cam_pos += glm::vec3(forward) * dt * speed;
        }
        if (m_player.keys['D'])
        {
            const glm::vec4 forward = glm::vec4{1, 0, 0, 1} * view;
            m_player.cam_pos += glm::vec3(forward) * dt * speed;
        }
        if (m_player.keys['Q'])
        {
            m_player.cam_pos.y -= dt * speed;
        }
        if (m_player.keys['E'])
        {
            m_player.cam_pos.y += dt * speed;
        }
        if (fabs(gamepad.thumbstick_left[0]) > dead_zone)
        {
            const glm::vec4 forward = glm::vec4{1, 0, 0, 1} * view;
            m_player.cam_pos += glm::vec3(forward) * dt * gamepad.thumbstick_left[0] * speed;
        }
        if (fabs(gamepad.thumbstick_left[1]) > dead_zone)
        {
            const glm::vec4 forward = glm::vec4{0, 0, -1, 1} * view;
            m_player.cam_pos += glm::vec3(forward) * dt * gamepad.thumbstick_left[1] * speed;
        }
        if (fabs(touch.thumbstick_left.x) > dead_zone)
        {
            const glm::vec4 forward = glm::vec4{1, 0, 0, 1} * view;
            m_player.cam_pos += glm::vec3(forward) * dt * touch.thumbstick_left.x * speed;
        }
        if (fabs(touch.thumbstick_left.y) > dead_zone)
        {
            const glm::vec4 forward = glm::vec4{0, 0, -1, 1} * view;
            m_player.cam_pos += glm::vec3(forward) * dt * touch.thumbstick_left.y * speed;
        }
    }
    void tick_xrmode(const float dt, const GamepadState& gamepad) noexcept
    {
        ZoneScoped;
        m_xr->poll_events();
        if (m_xr->state_active())
        {
            if (m_xr->state_focused() && !m_xr->sync_input())
            {
                LOGE("Failed to sync input");
            }

            vk::utils::FrameContext frame_fixed;
            m_xr->present(
            [this, dt, &gamepad, &frame_fixed](const vk::utils::FrameContext& frame)
            {
                frame_fixed = frame;
                update_flying_camera_angles(dt, gamepad, m_xr->touch_controllers());
                const auto cam_rot = glm::gtx::eulerAngleX(m_player.cam_angles.y) * glm::gtx::eulerAngleY(m_player.cam_angles.x);
                const glm::mat4 head_rot = glm::inverse(glm::gtc::mat4_cast(frame.view_quat[0]));
                update_flying_camera_pos(dt, gamepad, m_xr->touch_controllers(), head_rot * cam_rot);
                for (uint32_t i = 0; i < 2; i++)
                    frame_fixed.view[i] = frame.view[i] * glm::inverse(cam_rot * glm::gtx::translate(m_player.cam_pos));
                update(frame_fixed);
            },
            [this, dt, &frame_fixed](const vk::utils::FrameContext& frame){
                frame_fixed.cmd = frame.cmd;
                render(frame_fixed, dt, frame.cmd);
            });
        }
        else
        {
            // Submit empty frames
            const auto nop = [](const vk::utils::FrameContext&){};
            m_xr->present(nop, nop);
        }
    }
    void tick_windowed(const float dt, const GamepadState& gamepad) noexcept
    {
        ZoneScoped;
        vk::utils::FrameContext frame_fixed;
        m_vk->present(
        [this, dt, &gamepad, &frame_fixed](const vk::utils::FrameContext& frame)
        {
            frame_fixed = frame;
            const auto cam_rot = update_flying_camera_angles(dt, gamepad, {});
            update_flying_camera_pos(dt, gamepad, {}, frame.view[0] * glm::inverse(cam_rot));
            const float aspect = static_cast<float>(m_size.x) / static_cast<float>(m_size.y);
            frame_fixed.projection[0] = glm::gtc::perspectiveRH_ZO(glm::radians(m_player.cam_fov), aspect, 0.01f, 100.f);
            frame_fixed.projection[0][1][1] *= -1.0f; // flip Y for Vulkan
            frame_fixed.view[0] = glm::inverse(glm::gtx::translate(m_player.cam_pos) * cam_rot);

            update(frame_fixed);
        },
        [this, dt, &frame_fixed](const vk::utils::FrameContext& frame)
        {
            const VkImageMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .image = frame.resolve_color_image,
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            };
            vkCmdPipelineBarrier(frame.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            frame_fixed.cmd = frame.cmd;
            render(frame_fixed, dt, frame.cmd);
        });
    }
    void tick(const float dt, const GamepadState& gamepad) noexcept
    {
        ZoneScoped;
        if (xrmode)
        {
            m_timeline_value = m_xr->timeline_value().value_or(0);
            garbage_collect(false);
            tick_xrmode(dt, gamepad);
        }
        else
        {
            m_timeline_value = m_vk->timeline_value().value_or(0);
            garbage_collect(false);
            tick_windowed(dt, gamepad);
        }
    }
    void garbage_collect(const bool force) noexcept
    {
        ZoneScoped;
        std::multimap<uint64_t, std::pair<vk::Buffer&, const vk::BufferSuballocation>> not_deleted;
        uint32_t removed_count = 0;
        for (auto& [timeline, buffer_pair] : m_delete_buffers)
        {
            if (m_timeline_value >= timeline || force)
            {
                auto& [buffer, sb] = buffer_pair;
                buffer.subfree(sb);
                removed_count++;
            }
            else
            {
                not_deleted.emplace(timeline, buffer_pair);
            }
        }
        // LOGE("\rGC: removed %d / %llu", removed_count, m_delete_buffers.size());
        m_delete_buffers = std::move(not_deleted);
    }
    void on_resize(const uint32_t width, const uint32_t height) noexcept
    {
        LOGI("resize %d %d", width, height);
        m_size = { width, height };
    }
    void on_mouse_wheel(const int32_t x, const int32_t y, const float delta) noexcept
    {
        LOGI("mouse wheel %d %d %f", x, y, delta);
        m_player.cam_fov += delta * 10;
    }
    void on_mouse_move(const int32_t x, const int32_t y) noexcept
    {
        // LOGI("mouse moved %d %d", x, y);
        if (m_player.dragging)
        {
            const glm::ivec2 pos{x, y};
            const glm::ivec2 delta = pos - m_player.drag_start;
            constexpr float multiplier = .25f;
            m_player.cam_angles = m_player.cam_start + glm::radians(glm::vec2(delta) * multiplier);
        }
    }
    void on_mouse_left_down(const int32_t x, const int32_t y) noexcept
    {
        // LOGI("mouse left down %d %d", x, y);
        m_player.dragging = true;
        m_player.drag_start = {x, y};
        m_player.cam_start = m_player.cam_angles;
    }
    void on_mouse_left_up(const int32_t x, const int32_t y) noexcept
    {
        // LOGI("mouse left up %d %d", x, y);
        m_player.dragging = false;
    }
    void on_mouse_right_down(const int32_t x, const int32_t y) noexcept
    {
        // LOGI("mouse right down %d %d", x, y);
    }
    void on_mouse_right_up(const int32_t x, const int32_t y) noexcept
    {
        // LOGI("mouse right up %d %d", x, y);
    }
    void on_key_down(const uint64_t keycode) noexcept
    {
        //LOGI("keycode %llu", keycode);
#ifdef _WIN32
        if (keycode == VK_ESCAPE)
        {
            // Please don't kill me like that :(
            exit(0);
        }
#endif
        m_player.keys[keycode] = true;
    }
    void on_key_up(const uint64_t keycode) noexcept
    {
        m_player.keys[keycode] = false;
    }
};
}
