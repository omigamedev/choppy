module;
#include <format>
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
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#elifdef _WIN32
#include <windows.h>
#define LOGE(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

export module ce.app:chunksman;
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
import :globals;
import :systems;

export namespace ce::app::chunksman
{
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
struct BatchDraw
{
    std::vector<shaders::SolidFlatShader::PerObjectBuffer> ubo;
    std::vector<VkDrawIndirectCommand> draw_args;
    uint32_t draw_count = 0;
};
struct ChunksManager
{
    std::vector<Chunk> m_chunks;
    FlatGenerator generator{globals::ChunkSize, 10};
    GreedyMesher<shaders::SolidFlatShader::VertexInput> mesher{};
    // std::vector<uint32_t> m_chunk_updates;
    TracyLockable(std::mutex, m_chunks_mutex);
    std::thread m_chunks_thread;
    std::unordered_map<BlockLayer, ChunksState> m_chunks_state;
    std::vector<glm::ivec3> m_regenerate_sectors;
    std::atomic_bool needs_update = false;
    bool m_running = true;
    Frustum m_frustum;
    glm::vec3 cam_pos = { 0, 100, 0 };
    glm::ivec3 cam_sector = { 0, 0, 0 };

    bool create() noexcept
    {
        generator.load();
        m_chunks_thread = std::thread(&ChunksManager::generate_thread, this);
        return true;
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
    void destroy() noexcept
    {
        m_running = false;
        if (m_chunks_thread.joinable())
            m_chunks_thread.join();
        generator.save();
        for (auto& c : m_chunks)
        {
            for (auto& [k, b] : c.buffer)
                globals::m_resources->vertex_buffer.subfree(b);
        }
        clear_chunks_state(0);
    }
    [[nodiscard]] std::vector<glm::ivec3> generate_neighbors(const glm::ivec3 origin,
        const int32_t size) const noexcept
    {
        ZoneScoped;
        std::vector<glm::ivec3> neighbors;
        const uint32_t chunk_count = utils::pow(size * 2 + 1, 3);
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
        constexpr uint32_t chunk_count =
            (globals::ChunkRings * 2 + 1) * (globals::ChunkRings * 2 + 1) * (3);

        const glm::vec3 cur_sector =
            glm::floor(cam_pos / (globals::ChunkSize * globals::BlockSize));

        auto neighbors = generate_neighbors(cur_sector, globals::ChunkRings);
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
            if (auto it = std::ranges::find(neighbors, m_chunks[i].sector);
                it == neighbors.end() || m_chunks[i].regenerate)
            {
                chunk_indices.emplace_back(i);
            }
        }

        //const glm::vec3 dist = glm::abs(cur_sector - glm::vec3(m_player.cam_sector));
        if (m_chunks.size() < chunk_count ||
            !chunk_indices.empty() ||
            cam_sector != glm::ivec3(glm::floor(cur_sector)))
        {
            cam_sector = cur_sector;
            if (m_chunks.size() < chunk_count)
            {
                LOGI("Loading world: %d%%",
                    static_cast<int32_t>(m_chunks.size() * 100.f / chunk_count));
            }
            else
            {
                LOGI("travel to sector [%d, %d, %d]", cam_sector.x, cam_sector.y, cam_sector.z);
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
                auto chunk_data = mesher.mesh(blocks_data, globals::BlockSize);
                // m_chunk_updates.emplace_back(m_chunks.size());
                auto& chunk = m_chunks.emplace_back();
                chunk.mesh = std::move(chunk_data);
                chunk.data = std::move(blocks_data);
                chunk.color = glm::gtc::linearRand(glm::vec4(0, 0, 0, 1), glm::vec4(1, 1, 1, 1));
                chunk.sector = sector;
                chunk.transform = glm::gtc::translate(glm::vec3(sector) *
                    globals::ChunkSize * globals::BlockSize);
                chunk.dirty = true;
                chunk.regenerate = false;
                systems::m_physics_system->remove_body(chunk.body_id);
                if (auto result = systems::m_physics_system->create_chunk_body(
                    globals::ChunkSize, globals::BlockSize, chunk.data, chunk.sector))
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
                auto chunk_data = mesher.mesh(blocks_data, globals::BlockSize);
                const uint32_t chunk_index = chunk_indices[chunk_indices_offset++];
                auto& chunk = m_chunks[chunk_index];
                chunk.mesh = std::move(chunk_data);
                chunk.data = std::move(blocks_data);
                chunk.color = glm::gtc::linearRand(glm::vec4(0, 0, 0, 1), glm::vec4(1, 1, 1, 1));
                chunk.sector = sector;
                chunk.transform = glm::gtc::translate(glm::vec3(sector) *
                    globals::ChunkSize * globals::BlockSize);
                chunk.dirty = true;
                chunk.regenerate = false;
                systems::m_physics_system->remove_body(chunk.body_id);
                if (auto result = systems::m_physics_system->create_chunk_body(
                    globals::ChunkSize, globals::BlockSize, chunk.data, chunk.sector))
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
            {
                globals::m_resources->delete_buffers.emplace(timeline_value,
                   std::pair(std::ref(globals::m_resources->object_buffer), state.uniform_buffer));
            }
            if (state.args_buffer.alloc)
            {
                globals::m_resources->delete_buffers.emplace(timeline_value,
                   std::pair(std::ref(globals::m_resources->args_buffer), state.args_buffer));
            }
        }
        m_chunks_state.clear();
    }
    void update_chunks(const vk::utils::FrameContext& frame) noexcept
    {
        ZoneScoped;

        std::unique_lock lock(m_chunks_mutex, std::try_to_lock);
        if (!lock.owns_lock())
            return;

        // Update the frustum with the latest camera matrix for the primary view
        //if (update_frustum)
        {
            const auto vp = frame.projection[0] * frame.view[0];
            m_frustum.update(vp);
        }

        auto sorted_chunks = utils::sorted_view(m_chunks, [this](const auto& c1, const auto& c2) -> bool
        {
            const float dist1 = glm::gtx::distance2(glm::vec3(c1.sector), glm::vec3(cam_pos));
            const float dist2 = glm::gtx::distance2(glm::vec3(c2.sector), glm::vec3(cam_pos));
            return dist1 < dist2;
        });

        std::unordered_map<BlockLayer, BatchDraw> batches;
        for (auto& chunk : sorted_chunks)
        {
            // AABB Culling Check
            constexpr float chunk_world_size = globals::ChunkSize * globals::BlockSize;
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
                    {
                        globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                            std::pair(std::ref(globals::m_resources->vertex_buffer), chunk.buffer[layer]));
                    }
                    chunk.buffer.erase(layer);
                    continue;
                }
                if (chunk.dirty)
                {
                    if (const auto sb = globals::m_resources->staging_buffer.suballoc(m.vertices.size() *
                        sizeof(shaders::SolidFlatShader::VertexInput), 64))
                    {
                        if (const auto dst_sb = globals::m_resources->vertex_buffer.suballoc(sb->size, 64))
                        {
                            std::ranges::copy(m.vertices, static_cast<shaders::SolidFlatShader::VertexInput*>(sb->ptr));
                            globals::m_resources->copy_buffers.emplace_back(globals::m_resources->vertex_buffer, *sb, dst_sb->offset);
                            //m_chunks_state.vertex_buffer = *dst_sb;

                            if (chunk.buffer[layer].alloc)
                            {
                                globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                                   std::pair(std::ref(globals::m_resources->vertex_buffer), chunk.buffer[layer]));
                            }
                            chunk.buffer[layer] = *dst_sb;
                        }
                        // defer suballocation deletion
                        globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                            std::pair(std::ref(globals::m_resources->staging_buffer), *sb));
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
            if (const auto sb = globals::m_resources->staging_buffer.suballoc(batch.ubo.size() *
                sizeof(shaders::SolidFlatShader::PerObjectBuffer), 64))
            {
                if (const auto dst_sb = globals::m_resources->object_buffer.suballoc(sb->size, 64))
                {
                    auto ubo_copy = batch.ubo;
                    if (layer == BlockLayer::Transparent)
                    {
                        for (auto& ubo : ubo_copy)
                            ubo.y_offset = -0.1f;
                    }
                    std::ranges::copy(ubo_copy, static_cast<shaders::SolidFlatShader::PerObjectBuffer*>(sb->ptr));
                    globals::m_resources->copy_buffers.emplace_back(globals::m_resources->object_buffer, *sb, dst_sb->offset);
                    m_chunks_state[layer].uniform_buffer = *dst_sb;
                }
                // defer suballocation deletion
                globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                    std::pair(std::ref(globals::m_resources->staging_buffer), *sb));
            }

            if (const auto sb = globals::m_resources->staging_buffer.suballoc(batch.draw_args.size() * sizeof(VkDrawIndirectCommand), 64))
            {
                if (const auto dst_sb = globals::m_resources->args_buffer.suballoc(sb->size, 64))
                {
                    std::ranges::copy(batch.draw_args, static_cast<VkDrawIndirectCommand*>(sb->ptr));
                    globals::m_resources->copy_buffers.emplace_back(globals::m_resources->args_buffer, *sb, dst_sb->offset);
                    m_chunks_state[layer].args_buffer = *dst_sb;
                }
                // defer suballocation deletion
                globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                    std::pair(std::ref(globals::m_resources->staging_buffer), *sb));
            }
            m_chunks_state[layer].draw_count = batch.draw_count;
        }
        needs_update = false;
    }
    [[nodiscard]] std::optional<std::tuple<glm::ivec3, BlockType, glm::vec3, glm::ivec3>> trace_dda(const glm::vec3& origin,
        const glm::vec3& direction, const float dist, const auto hit_test) const noexcept
        requires std::invocable<decltype(hit_test), const BlockType> &&
             std::same_as<std::invoke_result_t<decltype(hit_test), const BlockType>, bool>
    {
        const auto hits = utils::traverse_3d_dda(origin, direction, glm::vec3(0), dist, globals::BlockSize);
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
            const glm::ivec3 cell = glm::floor(p / globals::BlockSize);
            // const glm::ivec3 sector = glm::floor(p / (BlockSize * ChunkSize));
            const BlockType b = generator.peek(cell);
            if (hit_test(b))
            {
                return std::tuple(cell, b, p);
            }
        }
        return std::nullopt;
    }
    void break_block(const glm::vec3& origin, const glm::vec3& direction) noexcept
    {
        if (const auto hit = trace_dda(origin, direction, 10.0,
            [](const BlockType b){ return b != BlockType::Air && b != BlockType::Water; }))
        {
            const auto [cell, b, p, n] = hit.value();
            const glm::ivec3 sector = glm::floor(glm::vec3(cell) / static_cast<float>(globals::ChunkSize));
            //if (!m_server_mode)
            // {
            //     const messages::BlockActionMessage message{
            //         .action = messages::BlockActionMessage::ActionType::Destroy,
            //         .world_cell = cell
            //     };
            //     systems::m_client_system->send_message(message);
            // }
            /*
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
            */
        }
    }
    void build_block(const glm::vec3& origin, const glm::vec3& direction) noexcept
    {
        if (const auto cell = build_block_cell(origin, direction))
        {
            const glm::ivec3 sector = glm::floor(glm::vec3(*cell) / static_cast<float>(globals::ChunkSize));
            //if (!m_server_mode)
            // {
            //     const messages::BlockActionMessage message{
            //         .action = messages::BlockActionMessage::ActionType::Build,
            //         .world_cell = cell.value()
            //     };
            //     systems::m_client_system->send_message(message);
            // }
            /*
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
            */
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
};
}
