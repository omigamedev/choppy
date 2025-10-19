module;
#include <array>
#include <span>
#include <vector>
#include <memory>
#include <ranges>
#include <concepts>
#include <functional>
#include <thread>
#include <map>
#include <mutex>

#include <volk.h>
#include <vk_mem_alloc.h>
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>
#include <tracy/TracyVulkan.hpp>
#include <Jolt/Jolt.h>

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
import ce.vk.buffer;
import ce.vk.texture;
import ce.vk.shader;
import ce.vk.utils;
import ce.shaders.solidflat;
import ce.shaders.solidcolor;
import glm;
import :utils;
import :frustum;
import :chunkgen;
import :chunkmesh;
import :player;

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
struct ChunkUpdate : utils::NoCopy
{
    std::map<BlockType, ChunkMesh<shaders::SolidFlatShader::VertexInput>> data;
    size_t chunk_index = 0;
    glm::vec4 color{};
    glm::ivec3 sector{};
};
struct ChunksState
{
    VkDescriptorSet object_descriptor_set = VK_NULL_HANDLE;
    //vk::BufferSuballocation vertex_buffer{};
    vk::BufferSuballocation uniform_buffer{};
    vk::BufferSuballocation args_buffer{};
    uint32_t draw_count = 0;
};
struct Geometry
{
    VkDescriptorSet object_descriptor_set = VK_NULL_HANDLE;
    vk::BufferSuballocation vertex_buffer{};
    vk::BufferSuballocation uniform_buffer{};
    uint32_t vertex_count = 0;
};
struct TransformComponent
{
    glm::vec3 translation{};
    glm::vec3 rotation{};
    glm::vec3 scale{};
    glm::mat4 transform{};
};
struct StaticMeshComponent
{
    Geometry geometry;
};

class AppBase final
{
    std::shared_ptr<xr::Context> m_xr;
    std::shared_ptr<vk::Context> m_vk;
    std::shared_ptr<shaders::SolidFlatShader> shader_opaque;
    std::shared_ptr<shaders::SolidFlatShader> shader_transparent;
    std::shared_ptr<shaders::SolidColorShader> shader_color;
    shaders::SolidFlatShader::PerFrameConstants uniform{};
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

    vk::texture::Texture m_texture;
    VkSampler m_sampler = VK_NULL_HANDLE;

    Geometry m_cube{};
    vk::BufferSuballocation shader_flat_frame{};
    vk::BufferSuballocation shader_color_frame{};
    VkDescriptorSet shader_flat_frame_set = VK_NULL_HANDLE;
    VkDescriptorSet shader_color_frame_set = VK_NULL_HANDLE;

    vk::Buffer m_args_buffer;
    bool m_running = true;
    player::PlayerState m_player{};
    bool xrmode = false;
    uint32_t m_swapchain_count = 0;
    uint64_t m_timeline_value = 0;

    // Size of a block in meters
    static constexpr float BlockSize = 0.5f;
    // Number of blocks per chunk
    static constexpr uint32_t ChunkSize = 32;
    static constexpr uint32_t ChunkRings = 4;

    FlatGenerator generator{ChunkSize, 10};
    GreedyMesher<shaders::SolidFlatShader::VertexInput> mesher{};
    // std::vector<uint32_t> m_chunk_updates;
    TracyLockable(std::mutex, m_chunks_mutex);
    std::thread m_chunks_thread;
    std::unordered_map<BlockLayer, ChunksState> m_chunks_state;
    std::vector<glm::ivec3> m_regenerate_sectors;
    std::atomic_bool needs_update = false;

    physics::PhysicsSystem m_physics_system;

    Frustum m_frustum;
    bool update_frustum = true;

public:
    ~AppBase()
    {
        m_running = false;
        if (m_vk && m_vk->device())
        {
            vkDeviceWaitIdle(m_vk->device());
        }
        if (m_chunks_thread.joinable())
            m_chunks_thread.join();
        for (auto& c : m_chunks)
        {
            for (auto& [k, b] : c.buffer)
                m_vertex_buffer.subfree(b);
        }
        if (m_cube.vertex_buffer.alloc)
            m_vertex_buffer.subfree(m_cube.vertex_buffer);
        if (m_cube.uniform_buffer.alloc)
            m_object_buffer.subfree(m_cube.uniform_buffer);
        clear_chunks_state(0);
        garbage_collect(true);
        generator.save();
    };
    [[nodiscard]] auto& xr() noexcept { return m_xr; }
    [[nodiscard]] auto& vk() noexcept { return m_vk; }

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

        m_physics_system.create_system();
        m_physics_system.create_shared_box(BlockSize);
        m_player.character = m_physics_system.create_character();

        generator.load();

        shader_opaque = std::make_shared<shaders::SolidFlatShader>(m_vk, "Opaque");
        shader_opaque->create(m_renderpass, m_swapchain_count, m_sample_count, 1, 100, false, false);
        shader_transparent = std::make_shared<shaders::SolidFlatShader>(m_vk, "Transparent");
        shader_transparent->create(m_renderpass, m_swapchain_count, m_sample_count, 1, 100, true, true);
        shader_color = std::make_shared<shaders::SolidColorShader>(m_vk, "Color");
        shader_color->create(m_renderpass, m_swapchain_count, m_sample_count, 1, 100, true, true, true);

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
        if (!m_frame_buffer.create(sizeof(shaders::SolidFlatShader::PerFrameConstants) * 100,
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

        m_cube = create_cube();

        m_vk->exec_immediate("init resources", [this](VkCommandBuffer cmd){
            if (xrmode)
            {
                m_xr->init_resources(cmd);
            }
            else
            {
                m_vk->init_resources(cmd);
            }
            flush_staging_buffer_copies(cmd);
        });

        m_texture = vk::texture::load_texture(m_vk, "assets/grass.png", m_staging_buffer).value();

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
        if (generate_chunks())
            needs_update.store(true);
        m_chunks_thread = std::thread(&AppBase::generate_thread, this);
    }
    [[nodiscard]] Geometry create_cube() noexcept
    {
        Geometry cube;
        // Define the 8 vertices of the cube with unique colors for interpolation
        constexpr std::array<shaders::SolidColorShader::VertexInput, 8> points = {{
            // {position},
            {{0, 0, 1, 1.0f}}, // 0: Front-Bottom-Left,  Red
            {{1, 0, 1, 1.0f}}, // 1: Front-Bottom-Right, Green
            {{1, 1, 1, 1.0f}}, // 2: Front-Top-Right,    Blue
            {{0, 1, 1, 1.0f}}, // 3: Front-Top-Left,     Yellow
            {{0, 0, 0, 1.0f}}, // 4: Back-Bottom-Left,   Magenta
            {{1, 0, 0, 1.0f}}, // 5: Back-Bottom-Right,  Cyan
            {{1, 1, 0, 1.0f}}, // 6: Back-Top-Right,     White
            {{0, 1, 0, 1.0f}}, // 7: Back-Top-Left,      Gray
        }};

        // Define the 36 indices for the 12 triangles of the cube
        constexpr std::array<uint32_t, 36> indices = {
            // Front face (+Z)
            0, 1, 2,   2, 3, 0,
            // Back face (-Z)
            4, 7, 6,   6, 5, 4,
            // Top face (+Y)
            3, 2, 6,   6, 7, 3,
            // Bottom face (-Y)
            4, 5, 1,   1, 0, 4,
            // Right face (+X)
            1, 5, 6,   6, 2, 1,
            // Left face (-X)
            4, 0, 3,   3, 7, 4,
        };

        std::vector<shaders::SolidColorShader::VertexInput> vertices;
        vertices.reserve(indices.size() * 3);
        for (const auto i : indices)
        {
            vertices.emplace_back(points[i]);
        }
        cube.vertex_count = static_cast<uint32_t>(vertices.size());

        constexpr shaders::SolidColorShader::PerObjectBuffer ubo{
            .ObjectTransform = glm::gtc::identity<glm::mat4>(),
            .Color = {1, 0, 0, 1}
        };

        if (const auto sb = m_staging_buffer.suballoc(vertices.size() *
            sizeof(shaders::SolidColorShader::VertexInput), 64))
        {
            if (const auto dst_sb = m_vertex_buffer.suballoc(sb->size, 64))
            {
                std::ranges::copy(vertices, static_cast<shaders::SolidColorShader::VertexInput*>(sb->ptr));
                m_copy_buffers.emplace_back(m_vertex_buffer, *sb, dst_sb->offset);
                cube.vertex_buffer = *dst_sb;
            }
            // defer suballocation deletion
            m_delete_buffers.emplace(1, std::pair(std::ref(m_staging_buffer), *sb));
        }
        return cube;
    }
    void generate_thread() noexcept
    {
        tracy::SetThreadName("generate_thread");
        while (m_running)
        {
            if (generate_chunks())
                needs_update.store(true);
        }
    }
    [[nodiscard]] std::vector<glm::ivec3> generate_neighbors(const glm::ivec3 origin, const int32_t size) const noexcept
    {
        ZoneScoped;
        std::vector<glm::ivec3> neighbors;
        const uint32_t chunk_count = pow(size * 2 + 1, 3);
        neighbors.reserve(chunk_count);
        const int32_t side = size;
        for (int32_t y = -1; y < 10 + 1; ++y)
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
        constexpr uint32_t chunk_count = (ChunkRings * 2 + 1) * (ChunkRings * 2 + 1) * (3);

        const glm::vec3 cur_sector = glm::floor(m_player.cam_pos / (ChunkSize * BlockSize));

        auto neighbors = generate_neighbors(cur_sector, ChunkRings);
        std::ranges::sort(neighbors, [cur_sector](const glm::ivec3 a, const glm::ivec3 b)
        {
            const float dist1 = glm::gtx::distance2(glm::vec3(a), cur_sector);
            const float dist2 = glm::gtx::distance2(glm::vec3(b), cur_sector);
            return dist1 < dist2;
        });

        std::vector<size_t> chunk_indices;
        chunk_indices.reserve(chunk_count);
        for (size_t i = 0; i < m_chunks.size(); ++i)
        {
            if (auto it = std::ranges::find(neighbors, m_chunks[i].sector); it == neighbors.end() || m_chunks[i].regenerate)
            {
                chunk_indices.emplace_back(i);
            }
        }

        //const glm::vec3 dist = glm::abs(cur_sector - glm::vec3(m_player.cam_sector));
        if (m_chunks.size() < chunk_count || !chunk_indices.empty() || m_player.cam_sector != glm::ivec3(glm::floor(cur_sector)))
        {
            m_player.cam_sector = cur_sector;
            if (m_chunks.size() < chunk_count)
            {
                LOGI("Loading world: %d%%", static_cast<int32_t>(m_chunks.size() * 100.f / chunk_count));
            }
            else
            {
                LOGI("travel to sector [%d, %d, %d]",
                    m_player.cam_sector.x, m_player.cam_sector.y, m_player.cam_sector.z);
            }
        }
        else
        {
            return false;
        }
        ZoneScoped;

        size_t chunk_indices_offset = 0;
        uint32_t chunks_to_generate = 1;
        std::lock_guard lock(m_chunks_mutex);
        for (const auto& sector : neighbors)
        {
            // check if it's already present
            if (auto it = std::ranges::find(m_chunks, sector,
                [](auto& p){ return p.sector; }); it != m_chunks.end())
            {
                if (!it->regenerate)
                    continue;
            }
            if (m_chunks.size() < chunk_count)
            {
                auto blocks_data = generator.generate(sector);
                auto chunk_data = mesher.mesh(blocks_data, BlockSize);
                // m_chunk_updates.emplace_back(m_chunks.size());
                auto& chunk = m_chunks.emplace_back();
                chunk.mesh = std::move(chunk_data);
                chunk.data = std::move(blocks_data);
                chunk.color = glm::gtc::linearRand(glm::vec4(0, 0, 0, 1), glm::vec4(1, 1, 1, 1));
                chunk.sector = sector;
                chunk.transform = glm::gtc::translate(glm::vec3(sector) * ChunkSize * BlockSize);
                chunk.dirty = true;
                chunk.regenerate = false;
                m_physics_system.remove_body(chunk.body_id);
                if (auto result = m_physics_system.create_chunk_body(ChunkSize, BlockSize, chunk.data, chunk.sector))
                {
                    std::tie(chunk.body_id, chunk.shape) = result.value();
                }
                needs_update = true;
                if (!--chunks_to_generate)
                    break;
            }
            else if (!chunk_indices.empty())
            {
                auto blocks_data = generator.generate(sector);
                auto chunk_data = mesher.mesh(blocks_data, BlockSize);
                const uint32_t chunk_index = chunk_indices[chunk_indices_offset++];
                auto& chunk = m_chunks[chunk_index];
                chunk.mesh = std::move(chunk_data);
                chunk.data = std::move(blocks_data);
                chunk.color = glm::gtc::linearRand(glm::vec4(0, 0, 0, 1), glm::vec4(1, 1, 1, 1));
                chunk.sector = sector;
                chunk.transform = glm::gtc::translate(glm::vec3(sector) * ChunkSize * BlockSize);
                chunk.dirty = true;
                chunk.regenerate = false;
                m_physics_system.remove_body(chunk.body_id);
                if (auto result = m_physics_system.create_chunk_body(ChunkSize, BlockSize, chunk.data, chunk.sector))
                {
                    std::tie(chunk.body_id, chunk.shape) = result.value();
                }
                needs_update = true;
                if (!--chunks_to_generate)
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
        for (const auto& [layer, state] : m_chunks_state)
        {
            if (state.uniform_buffer.alloc)
                m_delete_buffers.emplace(timeline_value, std::pair(std::ref(m_object_buffer), state.uniform_buffer));
            if (state.args_buffer.alloc)
                m_delete_buffers.emplace(timeline_value, std::pair(std::ref(m_args_buffer), state.args_buffer));
        }
        m_chunks_state.clear();
    }
    struct BatchDraw
    {
        std::vector<shaders::SolidFlatShader::PerObjectBuffer> ubo;
        std::vector<VkDrawIndirectCommand> draw_args;
        uint32_t draw_count = 0;
    };
    void update_chunks(const vk::utils::FrameContext& frame) noexcept
    {
        ZoneScoped;

        std::unique_lock lock(m_chunks_mutex, std::try_to_lock);
        if (!lock.owns_lock())
            return;
        
        // Update the frustum with the latest camera matrix for the primary view
        if (update_frustum)
        {
            const auto vp = frame.projection[0] * frame.view[0];
            m_frustum.update(vp);
        }

        auto sorted_chunks = utils::sorted_view(m_chunks, [this](const auto& c1, const auto& c2) -> bool
        {
            const float dist1 = glm::gtx::distance2(glm::vec3(c1.sector), glm::vec3(m_player.cam_pos));
            const float dist2 = glm::gtx::distance2(glm::vec3(c2.sector), glm::vec3(m_player.cam_pos));
            return dist1 < dist2;
        });

        std::unordered_map<BlockLayer, BatchDraw> batches;
        for (auto& chunk : sorted_chunks)
        {
            // AABB Culling Check
            constexpr float chunk_world_size = ChunkSize * BlockSize;
            const glm::vec3 min_corner = glm::vec3(chunk.sector) * chunk_world_size;
            const AABB chunk_aabb{
                .min = min_corner,
                .max = min_corner + glm::vec3(chunk_world_size),
            };

            // if (!m_frustum.is_box_visible(chunk_aabb))
            //     continue; // Skip this chunk, it's not visible

            for (const auto& [layer, m] : chunk.mesh)
            {
                if (m.vertices.empty())
                {
                    if (chunk.buffer[layer].alloc)
                        m_delete_buffers.emplace(frame.timeline_value, std::pair(std::ref(m_vertex_buffer), chunk.buffer[layer]));
                    chunk.buffer.erase(layer);
                    continue;
                }
                if (chunk.dirty)
                {
                    if (const auto sb = m_staging_buffer.suballoc(m.vertices.size() *
                        sizeof(shaders::SolidFlatShader::VertexInput), 64))
                    {
                        if (const auto dst_sb = m_vertex_buffer.suballoc(sb->size, 64))
                        {
                            std::ranges::copy(m.vertices, static_cast<shaders::SolidFlatShader::VertexInput*>(sb->ptr));
                            m_copy_buffers.emplace_back(m_vertex_buffer, *sb, dst_sb->offset);
                            //m_chunks_state.vertex_buffer = *dst_sb;

                            if (chunk.buffer[layer].alloc)
                                m_delete_buffers.emplace(frame.timeline_value, std::pair(std::ref(m_vertex_buffer), chunk.buffer[layer]));
                            chunk.buffer[layer] = *dst_sb;
                        }
                        // defer suballocation deletion
                        m_delete_buffers.emplace(frame.timeline_value, std::pair(std::ref(m_staging_buffer), *sb));
                    }
                }
                if (m_frustum.is_box_visible(chunk_aabb))
                {
                    batches[layer].ubo.push_back({
                        .ObjectTransform = glm::transpose(chunk.transform),
                    });
                    const auto vertex_offset = chunk.buffer[layer].offset / sizeof(shaders::SolidFlatShader::VertexInput);
                    batches[layer].draw_args.push_back({
                        .vertexCount = static_cast<uint32_t>(m.vertices.size()),
                        .instanceCount = 1,
                        .firstVertex = static_cast<uint32_t>(vertex_offset),
                        .firstInstance = 0
                    });
                    batches[layer].draw_count++;
                }
            }
            chunk.dirty = false;
            // chunk.mesh.clear();
        }
        //LOGI("drawing %d polys", polys / 3);
        clear_chunks_state(frame.timeline_value);
        for (const auto& [layer, batch] : batches)
        {
            if (const auto sb = m_staging_buffer.suballoc(batch.ubo.size() *
                sizeof(shaders::SolidFlatShader::PerObjectBuffer), 64))
            {
                if (const auto dst_sb = m_object_buffer.suballoc(sb->size, 64))
                {
                    auto ubo_copy = batch.ubo;
                    if (layer == BlockLayer::Transparent)
                    {
                        for (auto& ubo : ubo_copy)
                            ubo.y_offset = -0.1f;
                    }
                    std::ranges::copy(ubo_copy, static_cast<shaders::SolidFlatShader::PerObjectBuffer*>(sb->ptr));
                    m_copy_buffers.emplace_back(m_object_buffer, *sb, dst_sb->offset);
                    m_chunks_state[layer].uniform_buffer = *dst_sb;
                }
                // defer suballocation deletion
                m_delete_buffers.emplace(frame.timeline_value, std::pair(std::ref(m_staging_buffer), *sb));
            }

            if (const auto sb = m_staging_buffer.suballoc(batch.draw_args.size() * sizeof(VkDrawIndirectCommand), 64))
            {
                if (const auto dst_sb = m_args_buffer.suballoc(sb->size, 64))
                {
                    std::ranges::copy(batch.draw_args, static_cast<VkDrawIndirectCommand*>(sb->ptr));
                    m_copy_buffers.emplace_back(m_args_buffer, *sb, dst_sb->offset);
                    m_chunks_state[layer].args_buffer = *dst_sb;
                }
                // defer suballocation deletion
                m_delete_buffers.emplace(frame.timeline_value, std::pair(std::ref(m_staging_buffer), *sb));
            }
            m_chunks_state[layer].draw_count = batch.draw_count;
        }
        needs_update = false;
    }
    void update(const vk::utils::FrameContext& frame, glm::mat4 view) noexcept
    {
        ZoneScoped;
        shader_opaque->reset_descriptors(frame.present_index);
        shader_transparent->reset_descriptors(frame.present_index);
        shader_color->reset_descriptors(frame.present_index);

        update_chunks(frame);

        if (const auto sb = m_staging_buffer.suballoc(sizeof(shaders::SolidFlatShader::PerFrameConstants), 64))
        {
            const auto dst_sb = m_frame_buffer.suballoc(sb->size, 64);

            auto fogColor = glm::vec4(.27f, .37f, .5f, 1.f);
            auto fogStart = 20.f;
            auto fogEnd = 50.f;
            const glm::ivec3 cell = glm::floor(m_player.cam_pos / BlockSize);
            if (generator.peek(cell) == BlockType::Water)
            {
                fogColor = glm::vec4(0.f, 0.f, 0.05f, 1.f);
                fogStart = .0f;
                fogEnd = 10.f;
            }
            *static_cast<shaders::SolidFlatShader::PerFrameConstants*>(sb->ptr) = {
                .ViewProjection = {
                    glm::transpose(frame.projection[0] * frame.view[0]),
                    glm::transpose(frame.projection[1] * frame.view[1]),
                },
                .fogColor = fogColor,
                .fogStart = fogStart,
                .fogEnd = fogEnd,
            };
            // defer copy
            m_copy_buffers.emplace_back(m_frame_buffer, *sb, dst_sb->offset);
            shader_flat_frame = *dst_sb;
            // defer suballocation deletion
            m_delete_buffers.emplace(frame.timeline_value, std::pair(std::ref(m_staging_buffer), *sb));
            m_delete_buffers.emplace(frame.timeline_value, std::pair(std::ref(m_frame_buffer), *dst_sb));
            if (const auto set = shader_opaque->alloc_descriptor(frame.present_index, 0))
            {
                shader_flat_frame_set = *set;
                shader_opaque->write_buffer(*set, 0, m_frame_buffer.buffer(),
                    dst_sb->offset, dst_sb->size, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
                shader_opaque->write_texture(*set, 1, m_texture.image_view, m_sampler);
            }
        }

        if (const auto sb = m_staging_buffer.suballoc(sizeof(shaders::SolidColorShader::PerFrameConstants), 64))
        {
            const auto dst_sb = m_frame_buffer.suballoc(sb->size, 64);
            *static_cast<shaders::SolidColorShader::PerFrameConstants*>(sb->ptr) = {
                .ViewProjection = {
                    glm::transpose(frame.projection[0] * frame.view[0]),
                    glm::transpose(frame.projection[1] * frame.view[1]),
                }
            };
            // defer copy
            m_copy_buffers.emplace_back(m_frame_buffer, *sb, dst_sb->offset);
            shader_color_frame = *dst_sb;
            // defer suballocation deletion
            m_delete_buffers.emplace(frame.timeline_value, std::pair(std::ref(m_staging_buffer), *sb));
            m_delete_buffers.emplace(frame.timeline_value, std::pair(std::ref(m_frame_buffer), *dst_sb));
            if (const auto set = shader_color->alloc_descriptor(frame.present_index, 0))
            {
                shader_color_frame_set = *set;
                shader_color->write_buffer(*set, 0, m_frame_buffer.buffer(),
                    dst_sb->offset, dst_sb->size, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            }
        }

        for (auto& [k, state] : m_chunks_state)
        {
            const auto& shader = (k == BlockLayer::Transparent) ? shader_transparent : shader_opaque;
            if (const auto set = shader->alloc_descriptor(frame.present_index, 1))
            {
                state.object_descriptor_set = *set;
                shader->write_buffer(*set, 0, m_object_buffer.buffer(),
                    state.uniform_buffer.offset, state.uniform_buffer.size,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            }
        }

        if (const auto sb = m_staging_buffer.suballoc(sizeof(shaders::SolidColorShader::PerObjectBuffer), 64))
        {
            if (const auto dst_sb = m_object_buffer.suballoc(sb->size, 64))
            {
                const glm::vec4 forward = glm::vec4{0, 0, -1, 1} * view;
                if (auto hit = build_block_cell(m_player.cam_pos, forward))
                {
                    *static_cast<shaders::SolidColorShader::PerObjectBuffer*>(sb->ptr) = {
                        .ObjectTransform = glm::transpose(
                            glm::gtc::translate(glm::vec3(*hit) * BlockSize) * glm::gtx::scale(glm::vec3(BlockSize))),
                        .Color = {.1, .1, .1, 1}
                    };
                    // LOGI("hit: [%d, %d, %d]", hit->x, hit->y, hit->z);
                }
                else
                {
                    *static_cast<shaders::SolidColorShader::PerObjectBuffer*>(sb->ptr) = {
                        .ObjectTransform = glm::gtc::identity<glm::mat4>(),
                        .Color = {0, 0, 0, 1}
                    };
                }
                m_copy_buffers.emplace_back(m_object_buffer, *sb, dst_sb->offset);
                m_cube.uniform_buffer = *dst_sb;
                m_delete_buffers.emplace(frame.timeline_value, std::pair(std::ref(m_object_buffer), *dst_sb));
            }
            // defer suballocation deletion
            m_delete_buffers.emplace(frame.timeline_value, std::pair(std::ref(m_staging_buffer), *sb));
        }
        if (const auto set = shader_color->alloc_descriptor(frame.present_index, 1))
        {
            m_cube.object_descriptor_set = *set;
            shader_color->write_buffer(*set, 0, m_object_buffer.buffer(),
                m_cube.uniform_buffer.offset, m_cube.uniform_buffer.size,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        shader_opaque->update_descriptors();
        shader_transparent->update_descriptors();
        shader_color->update_descriptors();
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
            std::vector<VkBufferMemoryBarrier> barriers;
            barriers.reserve(m_chunks_state.size());
            for (const auto& [k, state] : m_chunks_state)
            {
                barriers.push_back({
                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .buffer = m_args_buffer.buffer(),
                    .offset = state.args_buffer.offset,
                    .size = state.args_buffer.size,
                });
            }

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                0, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data(), 0, nullptr);
        }

        // char zone_name[64];
        // snprintf(zone_name, sizeof(zone_name), "RenderPass %llu", static_cast<uint64_t>(frame.timeline_value));
        // TracyVkZoneTransient(m_vk->tracy(), render_pass_zone, cmd, zone_name, true);
        TracyVkZone(m_vk->tracy(), cmd, "RenderPass");

        // Renderpass setup
        constexpr std::array rgb{.27f, .37f, .5f};
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
        if (m_chunks_state.size() > 1)
        {
            const std::vector layers{
                std::pair(shader_opaque, m_chunks_state[BlockLayer::Solid]),
                std::pair(shader_transparent, m_chunks_state[BlockLayer::Transparent]),
            };
            for (const auto& [shader, state] : layers)
            {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline());

                const std::array vertex_buffers{m_vertex_buffer.buffer()};
                const std::array vertex_buffers_offset{VkDeviceSize{0ull}};
                vkCmdBindVertexBuffers(cmd, 0, static_cast<uint32_t>(vertex_buffers.size()),
                    vertex_buffers.data(), vertex_buffers_offset.data());

                const std::array sets{shader_flat_frame_set, state.object_descriptor_set};
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    shader->layout(), 0, sets.size(), sets.data(), 0, nullptr);

                vkCmdDrawIndirect(cmd, m_args_buffer.buffer(),
                    state.args_buffer.offset, state.draw_count, sizeof(VkDrawIndirectCommand));
            }
        }

        // draw cube
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shader_color->pipeline());
            vkCmdSetLineWidth(cmd, 2.f);
            //vkCmdSetPolygonModeEXT(cmd, VK_POLYGON_MODE_LINE);

            const std::array vertex_buffers{m_vertex_buffer.buffer()};
            const std::array vertex_buffers_offset{m_cube.vertex_buffer.offset};
            vkCmdBindVertexBuffers(cmd, 0, static_cast<uint32_t>(vertex_buffers.size()),
                vertex_buffers.data(), vertex_buffers_offset.data());

            const std::array sets{shader_color_frame_set, m_cube.object_descriptor_set};
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                shader_color->layout(), 0, sets.size(), sets.data(), 0, nullptr);

            vkCmdDraw(cmd, m_cube.vertex_count, 1, 0, 0);
        }

        // {
        //     vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shader_color->pipeline());
        //
        //     const std::array vertex_buffers{m_vertex_buffer.buffer()};
        //     const std::array vertex_buffers_offset{m_falling_cube.vertex_buffer.offset};
        //     vkCmdBindVertexBuffers(cmd, 0, static_cast<uint32_t>(vertex_buffers.size()),
        //         vertex_buffers.data(), vertex_buffers_offset.data());
        //
        //     const std::array sets{shader_color_frame_set, m_falling_cube.object_descriptor_set};
        //     vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        //         shader_color->layout(), 0, sets.size(), sets.data(), 0, nullptr);
        //
        //     vkCmdDraw(cmd, m_falling_cube.vertex_count, 1, 0, 0);
        // }

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

//        const glm::vec2 touch_abs_rthumb = glm::abs(touch.thumbstick_right);
//        if (touch_abs_rthumb.x > dead_zone)
//        {
//            m_player.cam_angles.x += glm::radians(touch.thumbstick_right.x * dt * steering_speed);
//        }
//        if (touch_abs_rthumb.y > dead_zone)
//        {
//            m_player.cam_angles.y -= glm::radians(-touch.thumbstick_right.y * dt * steering_speed);
//        }
        return glm::gtx::eulerAngleYX(-m_player.cam_angles.x, -m_player.cam_angles.y);
    }
    [[nodiscard]] std::optional<std::tuple<glm::ivec3, BlockType, glm::vec3>> trace(const glm::vec3& origin,
        const glm::vec3& direction, const float dist, const float step, const auto hit_test) const noexcept
        requires std::invocable<decltype(hit_test), const BlockType> &&
             std::same_as<std::invoke_result_t<decltype(hit_test), const BlockType>, bool>
    {
        const uint32_t max_iterations = static_cast<uint32_t>(glm::ceil(dist / step));
        for (uint32_t i = 0; i < max_iterations; ++i)
        {
            const float t = i * step;
            const glm::vec3 p = origin + t * direction;
            const glm::ivec3 cell = glm::floor(p / BlockSize);
            // const glm::ivec3 sector = glm::floor(p / (BlockSize * ChunkSize));
            const BlockType b = generator.peek(cell);
            if (hit_test(b))
            {
                return std::tuple(cell, b, p);
            }
        }
        return std::nullopt;
    }
    [[nodiscard]] std::optional<std::tuple<glm::ivec3, BlockType, glm::vec3, glm::ivec3>> trace_dda(const glm::vec3& origin,
        const glm::vec3& direction, const float dist, const auto hit_test) const noexcept
        requires std::invocable<decltype(hit_test), const BlockType> &&
             std::same_as<std::invoke_result_t<decltype(hit_test), const BlockType>, bool>
    {
        const auto hits = traverse_3d_dda(origin, direction, glm::vec3(0), dist);
        for (const auto& [cell, t, n] : hits)
        {
            const BlockType b = generator.peek(cell);
            if (hit_test(b))
            {
                return std::tuple(cell, b, origin + t * direction, n);
            }
        }
        return std::nullopt;
    }
    std::vector<std::tuple<glm::ivec3, float, glm::ivec3>> traverse_3d_dda(const glm::vec3& origin,
        const glm::vec3& direction, const glm::vec3& grid_origin_reference, const float max_t) const noexcept
    {
        std::vector<std::tuple<glm::ivec3, float, glm::ivec3>> traversed_voxels;
        // Grid coordinate of the ray's origin relative to the grid reference (0,0,0)
        const glm::vec3 relative_origin = origin - grid_origin_reference;

        // Direction sign for stepping (+1 or -1)
        const glm::ivec3 step{
            (direction.x >= 0.0f) ? 1 : -1,
            (direction.y >= 0.0f) ? 1 : -1,
            (direction.z >= 0.0f) ? 1 : -1,
        };

        constexpr float infinity = std::numeric_limits<float>::max();
        const glm::vec3 t_delta{
            (glm::abs(direction.x) < 1e-6f) ? infinity : BlockSize / glm::abs(direction.x),
            (glm::abs(direction.y) < 1e-6f) ? infinity : BlockSize / glm::abs(direction.y),
            (glm::abs(direction.z) < 1e-6f) ? infinity : BlockSize / glm::abs(direction.z),
        };

        // Helper lambda to calculate initial t_max for one axis
        const auto calculate_initial_t = [&](const float origin_comp,
            const float direction_comp, const int step_comp, const float grid_ref_comp) -> float
        {
            if (glm::abs(direction_comp) < 1e-6f) return max_t;

            const float rel_origin_comp = origin_comp - grid_ref_comp;
            const float cell_start = glm::floor(rel_origin_comp / BlockSize) * BlockSize;
            // Next boundary world coordinate
            const float boundary = (step_comp > 0) ? (cell_start + BlockSize) : cell_start;
            const float t = (boundary - rel_origin_comp) / direction_comp;
            // Correction for starting exactly on a boundary
            if (t < 1e-6f && step_comp < 0)
            {
                return BlockSize / glm::abs(direction_comp);
            }
            return glm::max(t, 0.0f);
        };

        // Calculate initial t_max for X, Y, Z
        // t_max: parametric distance 't' to the first grid plane intersection for each axis
        glm::vec3 t_max{
            calculate_initial_t(origin.x, direction.x, step.x, grid_origin_reference.x),
            calculate_initial_t(origin.y, direction.y, step.y, grid_origin_reference.y),
            calculate_initial_t(origin.z, direction.z, step.z, grid_origin_reference.z),
        };

        glm::ivec3 current_cell = glm::ivec3(glm::floor(relative_origin / BlockSize));

        // Initial cell is recorded at t=0 with a (0,0,0) normal (since no face was hit yet)
        traversed_voxels.emplace_back(current_cell, 0.0f, glm::ivec3(0));

        while (glm::gtx::compMin(t_max) < max_t)
        {
            glm::ivec3 normal(0); // Face normal for the plane being crossed
            // Determine the axis (and thus the plane) that will be crossed next
            if (t_max.x < t_max.y)
            {
                if (t_max.x < t_max.z)
                {
                    // X-axis is the shortest step
                    current_cell.x += step.x;
                    normal.x = -step.x; // Normal points opposite to the ray step direction
                    traversed_voxels.emplace_back(current_cell, t_max.x, normal);
                    t_max.x += t_delta.x;
                }
                else
                {
                    // Z-axis is the shortest step (or equal to X)
                    current_cell.z += step.z;
                    normal.z = -step.z;
                    traversed_voxels.emplace_back(current_cell, t_max.z, normal);
                    t_max.z += t_delta.z;
                }
            }
            else
            {
                if (t_max.y < t_max.z)
                {
                    // Y-axis is the shortest step
                    current_cell.y += step.y;
                    normal.y = -step.y;
                    traversed_voxels.emplace_back(current_cell, t_max.y, normal);
                    t_max.y += t_delta.y;
                }
                else
                {
                    // Z-axis is the shortest step (or equal to Y)
                    current_cell.z += step.z;
                    normal.z = -step.z;
                    traversed_voxels.emplace_back(current_cell, t_max.z, normal);
                    t_max.z += t_delta.z;
                }
            }
        }

        return traversed_voxels;
    }
    void break_block(const glm::vec3& origin, const glm::vec3& direction) noexcept
    {
        if (const auto hit = trace_dda(origin, direction, 10.0,
            [](const BlockType b){ return b != BlockType::Air && b != BlockType::Water; }))
        {
            const auto [cell, b, p, n] = hit.value();
            const glm::ivec3 sector = glm::floor(glm::vec3(cell) / static_cast<float>(ChunkSize));
            generator.remove(sector, cell - sector * static_cast<int32_t>(ChunkSize));
            std::lock_guard lock(m_chunks_mutex);
            if (auto it = std::ranges::find(m_chunks, sector, &Chunk::sector); it != m_chunks.end())
            {
                // auto blocks_data = generator.generate(sector);
                // auto chunk_data = mesher.mesh(blocks_data, BlockSize);
                // it->mesh = std::move(chunk_data);
                // it->data = std::move(blocks_data);
                // it->sector = sector;
                // it->dirty = true;
                // m_physics_system.remove_body(it->body_id);
                // if (auto result = m_physics_system.create_chunk_body(ChunkSize, BlockSize, it->data, it->sector))
                // {
                //     std::tie(it->body_id, it->shape) = result.value();
                // }
                // needs_update = true;
                it->regenerate = true;
            }
        }
    }
    void build_block(const glm::vec3& origin, const glm::vec3& direction) noexcept
    {
        if (const auto cell = build_block_cell(origin, direction))
        {
            const glm::ivec3 sector = glm::floor(glm::vec3(*cell) / static_cast<float>(ChunkSize));
            generator.edit(sector, *cell - sector * static_cast<int32_t>(ChunkSize), BlockType::Dirt);
            std::lock_guard lock(m_chunks_mutex);
            if (const auto it = std::ranges::find(m_chunks, sector, &Chunk::sector); it != m_chunks.end())
            {
                // auto blocks_data = generator.generate(sector);
                // auto chunk_data = mesher.mesh(blocks_data, BlockSize);
                // it->mesh = std::move(chunk_data);
                // it->data = std::move(blocks_data);
                // it->sector = sector;
                // it->dirty = true;
                // m_physics_system.remove_body(it->body_id);
                // if (auto result = m_physics_system.create_chunk_body(ChunkSize, BlockSize, it->data, it->sector))
                // {
                //     std::tie(it->body_id, it->shape) = result.value();
                // }
                // needs_update = true;
                it->regenerate = true;
            }
        }
    }
    std::optional<glm::ivec3> build_block_cell(const glm::vec3& origin, const glm::vec3& direction) const noexcept
    {
        if (const auto hit = trace_dda(origin, direction, 10.0,
            [](const BlockType b){ return b != BlockType::Air && b != BlockType::Water; }))
        {
            const auto [_cell, b, p, n] = hit.value();
            return _cell + n;
        }
        return std::nullopt;
    }
    void update_flying_camera_pos(const float dt, const GamepadState& gamepad,
        const xr::TouchControllerState& touch, const glm::mat4& view)
    {
        ZoneScoped;
        constexpr float dead_zone = 0.05;
#ifdef _WIN32
        float speed = 2.f;
        if (m_player.keys[VK_SHIFT])
            speed = speed * 0.25f;
        if (m_player.keys[VK_CONTROL])
            speed = speed * 2.f;
        const bool action_build_new = m_player.keys[VK_SPACE] ||
            gamepad.buttons[std::to_underlying(GamepadButton::ShoulderLeft)];
        const bool action_break_new = m_player.keys['B'] ||
            gamepad.buttons[std::to_underlying(GamepadButton::ShoulderRight)];
        const bool action_respawn_new = m_player.keys['F'] ||
            gamepad.buttons[std::to_underlying(GamepadButton::Menu)];
        const bool action_frustum_new = m_player.keys['C'];
        const bool action_physics_record_new = m_player.keys['R'];
        speed = speed + 10.f * static_cast<float>(gamepad.buttons[std::to_underlying(GamepadButton::ThumbLeft)]);
#else
        const float speed = 3.f;
        const bool action_build_new = touch.trigger_right > 0.5f;
        const bool action_break_new = touch.trigger_left > 0.5f;
        const bool action_respawn_new = touch.buttons[std::to_underlying(GamepadButton::Menu)];
        const bool action_frustum_new = touch.buttons[std::to_underlying(GamepadButton::X)];
        const bool action_physics_record_new = false;
#endif
        static bool action_physics_record = false;
        if (action_physics_record != action_physics_record_new)
        {
            action_physics_record = action_physics_record_new;
            if (action_physics_record)
            {
                m_physics_system.start_recording();
            }
        }
        static bool action_frustum = false;
        if (action_frustum != action_frustum_new)
        {
            action_frustum = action_frustum_new;
            if (action_frustum)
            {
                update_frustum = !update_frustum;
            }
        }
        static bool action_respawn = false;
        if (action_respawn != action_respawn_new)
        {
            action_respawn = action_respawn_new;
            if (action_respawn)
            {
                const auto p = m_player.character->GetPosition();
                m_player.character->SetPosition({p.GetX(), 20, p.GetZ()});
                m_player.character->SetLinearVelocity(JPH::Vec3::sZero());
            }
        }
        static bool action_build = false;
        if (action_build_new != action_build)
        {
            action_build = action_build_new;
            if (action_build)
            {
                const glm::vec4 forward = glm::vec4{0, 0, -1, 1} * view;
                build_block(m_player.cam_pos, forward);
            }
        }
        static bool action_break = false;
        if (action_break_new != action_break)
        {
            action_break = action_break_new;
            if (action_break)
            {
                const glm::vec4 forward = glm::vec4{0, 0, -1, 1} * view;
                break_block(m_player.cam_pos, forward);
            }
        }
#ifdef _WIN32
        const float fx = (fabs(gamepad.thumbstick_left[0]) > dead_zone ? gamepad.thumbstick_left[0] : 0.f) +
            (m_player.keys['D'] ? 1.f : 0.f) + (m_player.keys['A'] ? -1.f : 0.f);
        const float fz = (fabs(gamepad.thumbstick_left[1]) > dead_zone ? gamepad.thumbstick_left[1] : 0.f) +
            (m_player.keys['W'] ? 1.f : 0.f) + (m_player.keys['S'] ? -1.f : 0.f);
        const bool action_jump_new = m_player.keys['E'] ||
            gamepad.buttons[std::to_underlying(xr::TouchControllerButton::A)];
#else
        const float fx = fabs(touch.thumbstick_left[0]) > dead_zone ? touch.thumbstick_left[0] : 0.f;
        const float fz = fabs(touch.thumbstick_left[1]) > dead_zone ? touch.thumbstick_left[1] : 0.f;
        bool action_jump_new = touch.buttons[std::to_underlying(xr::TouchControllerButton::A)];
#endif
        float fy = 0.f;
        static bool action_jump = false;
        if (action_jump_new != action_jump)
        {
            action_jump = action_jump_new;
            if (action_jump && m_player.character->IsSupported())
                fy += 5.f;
        }
        const glm::vec3 forward = glm::vec4{fx, 0, -fz, 1} * view;
        if (glm::abs(glm::length(forward)) > 0.f || fy > 0.f)
        {
            const auto step = glm::vec3(forward) * speed;
            const auto current_velocity = glm::gtc::make_vec3(m_player.character->GetLinearVelocity().mF32);
            const auto velocity = glm::vec3(step.x, current_velocity.y, step.z);
            const auto v = glm::gtc::lerp(current_velocity, velocity, dt * 3.f);
            m_player.character->SetLinearVelocity(JPH::Vec3(v.x, fy + v.y, v.z));
        }
        else
        {
            const auto current_velocity = glm::gtc::make_vec3(m_player.character->GetLinearVelocity().mF32);
            const auto v = glm::gtc::lerp(current_velocity, glm::vec3(0, current_velocity.y, 0), dt * 3.f);
            m_player.character->SetLinearVelocity(JPH::Vec3(v.x, v.y, v.z));
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
                update(frame_fixed, head_rot * cam_rot);
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

            update(frame_fixed, frame.view[0] * glm::inverse(cam_rot));
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
        m_physics_system.tick(dt);
        m_player.character->PostSimulation(0.1);
        m_player.cam_pos = glm::gtc::make_vec3(m_player.character->GetPosition().mF32);

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
    void flush_staging_buffer_copies(VkCommandBuffer cmd) noexcept
    {
        TracyVkZone(m_vk->tracy(), cmd, "Flush Copy Buffers");
        for (auto& [buffer, sb, dst_offset] : m_copy_buffers)
        {
            buffer.copy_from(cmd, m_staging_buffer.buffer(), sb, dst_offset);
        }
        m_copy_buffers.clear();
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
