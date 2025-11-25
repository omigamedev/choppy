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

#include <enet.h>
#include <future>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>
#include <tracy/TracyVulkan.hpp>
#include <Jolt/Jolt.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#else
#define LOGE(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

export module ce.app:chunksman;
import glm;
import :globals;
import :frustum;
import :chunkmesh;
import :systems;
import :physics;
import :client;

export namespace ce::app::chunksman
{
struct Chunk final
{
    bool valid = false;
    glm::mat4 transform{};
    glm::vec4 color{};
    glm::ivec3 sector{};
    std::unordered_map<BlockLayer, ChunkMesh<shaders::SolidFlatShader::VertexInput>> mesh;
    std::unordered_map<BlockLayer, vk::BufferSuballocation> buffer{};
    bool dirty = false;
    bool regenerate = false;
    ChunkData data;
    JPH::RefConst<JPH::Shape> shape;
    JPH::BodyID body_id;
    bool net_sync = false;
    bool net_requested = false;
    std::future<void> generate_future;
    bool async_generating = false;
    uint32_t lod = 0;
};
struct ChunkUpdate
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
    std::vector<std::shared_ptr<Chunk>> m_chunks;
    FlatGenerator generator{globals::ChunkSize, 10};
    GreedyMesher<shaders::SolidFlatShader::VertexInput> mesher{};
    // std::vector<uint32_t> m_chunk_updates;
    TracyLockable(std::mutex, m_chunks_mutex);
    std::thread m_chunks_thread;
    std::unordered_map<BlockLayer, ChunksState> m_chunks_state;
    std::vector<glm::ivec3> m_regenerate_sectors;
    std::atomic_bool needs_update = false;
    std::vector<std::pair<glm::ivec3, std::vector<uint8_t>>> chunks_to_sync;
    enum class ChunkNetState{None, Wait, Ready, Sync};
    std::unordered_map<glm::ivec3, ChunkNetState, IVec3Hash> chunks_netstate;
    std::unordered_map<glm::ivec3, std::vector<uint8_t>, IVec3Hash> chunks_netdata;
    std::vector<glm::ivec3> sectors_to_request;
    std::vector<glm::ivec3> sectors_to_wait;
    bool m_running = true;
    Frustum m_frustum[2];
    glm::vec3 cam_pos = { 0, 10, 0 };
    glm::ivec3 cam_sector = { 0, 0, 0 };
    uint64_t last_timeline_value = 0;
    std::vector<glm::ivec3> neighbors;

    std::function<void(const glm::ivec3& sector)> on_sector_sync;
    std::function<void(const glm::ivec3& sector)> on_sector_drawing;

    bool create() noexcept
    {
        if (globals::server_mode)
        {
            generator.load();
        }
        else
        {
            if (generate_chunks(32))
                needs_update.store(true);
            m_chunks_thread = std::thread(&ChunksManager::generate_thread, this);
        }
        return true;
    }
    void generate_thread() noexcept
    {
        tracy::SetThreadName("generate_thread");
        while (m_running)
        {
            if (generate_chunks(10))
                needs_update.store(true);
        }
    }
    void destroy() noexcept
    {
        m_running = false;
        if (m_chunks_thread.joinable())
            m_chunks_thread.join();
        if (globals::server_mode)
            generator.save();
        for (auto& c : m_chunks)
        {
            for (auto& [k, b] : c->buffer)
                globals::m_resources->vertex_buffer.subfree(b);
        }
        clear_chunks_state(0);
    }
    [[nodiscard]] std::vector<glm::ivec3> generate_neighbors(const glm::ivec3 origin,
        const int32_t size) const noexcept
    {
        ZoneScoped;
        const uint32_t chunk_count = utils::pow(size * 2 + 1, 3);
        std::vector<glm::ivec3> neighbors;
        neighbors.reserve(chunk_count);
        for (int32_t y = -size; y < size + 1; ++y)
        {
            for (int32_t z = -size; z < size + 1; ++z)
            {
                for (int32_t x = -size; x < size + 1; ++x)
                {
                    const glm::ivec3 sector = glm::ivec3{x, y, z} +
                        glm::ivec3(origin.x, origin.y, origin.z);
                    neighbors.emplace_back(sector);
                }
            }
        }
        return neighbors;
    }
    [[nodiscard]] uint32_t chunk_lod(const glm::ivec3& chunk_sector, const glm::ivec3& cur_sector) const noexcept
    {
        //return 1;
        const float dist = glm::gtx::distance(glm::vec3(chunk_sector), glm::vec3(cur_sector));
        uint32_t lod = 1;
        if (dist >= 5)
            lod = 2;
        if (dist >= 8)
            lod = 4;
        if (dist >= 12)
            lod = 8;
        //if (dist >= 16)
        //    lod = 16;
        return lod;
    }
    [[nodiscard]] bool generate_chunks(uint32_t chunks_to_generate) noexcept
    {
        static bool i_have_done_something = false;
        static uint32_t things_done = 0;
        if (!i_have_done_something || things_done > 10)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            things_done = 0;
        }
        i_have_done_something = false;
        std::lock_guard lock(m_chunks_mutex);

        constexpr uint32_t chunk_count = utils::pow(globals::ChunkRings * 2 + 1, 3);
        const glm::ivec3 cur_sector =
            glm::floor(cam_pos / (globals::ChunkSize * globals::BlockSize));

        neighbors = generate_neighbors(cur_sector, globals::ChunkRings);
        std::ranges::sort(neighbors, [cur_sector](const glm::ivec3 a, const glm::ivec3 b)
        {
            const float dist1 = glm::gtx::distance2(glm::vec3(a), glm::vec3(cur_sector));
            const float dist2 = glm::gtx::distance2(glm::vec3(b), glm::vec3(cur_sector));
            return dist1 < dist2;
        });

        if (!globals::server_mode && systems::m_client_system->connected())
        {
            std::vector<glm::ivec3> sectors;
            for (const auto& sector : neighbors)
            {
                if (!chunks_netstate.contains(sector) && !std::ranges::contains(sectors_to_request, sector))
                {
                    sectors_to_request.emplace_back(sector);
                    i_have_done_something = true;
                }
            }
        }

        // for (auto& chunk : m_chunks)
        // {
        //
        // }

        std::vector<size_t> chunk_indices;
        chunk_indices.reserve(chunk_count);
        for (size_t i = 0; i < m_chunks.size(); ++i)
        {
            if (auto it = std::ranges::find(neighbors, m_chunks[i]->sector); it == neighbors.end())
            {
                chunk_indices.emplace_back(i);
            }
        }
        const auto regenerate_count = std::ranges::count_if(m_chunks, [](auto& p){ return p->regenerate; });
        if (m_chunks.size() < chunk_count || !chunk_indices.empty() ||
            cam_sector != cur_sector || regenerate_count > 0)
        {
            cam_sector = cur_sector;
            if (m_chunks.size() < chunk_count)
            {
                // LOGI("Loading world: %d%%",
                //     static_cast<int32_t>(m_chunks.size() * 100.f / chunk_count));
            }
            else
            {
                // LOGI("travel to sector [%d, %d, %d]", cam_sector.x, cam_sector.y, cam_sector.z);
            }
        }
        else
        {
            return false;
        }
        ZoneScoped;

        size_t chunk_indices_offset = 0;
        for (const auto& sector : neighbors)
        {
            if (const auto it = chunks_netstate.find(sector);
                it == chunks_netstate.end() || it->second == ChunkNetState::Wait)
            {
                continue;
            }
            // check if it's already present
            const auto it = std::ranges::find(m_chunks, sector, [](auto& p){ return p->sector; });
            if (it != m_chunks.end())
            {
                auto& chunk = *it;
                const uint32_t lod = chunk_lod(chunk->sector, cur_sector);
                if (chunk->regenerate || chunk->lod != lod)
                {
                    auto blocks_data = generator.generate(sector, lod);
                    if (!blocks_data.empty)
                    {
                        auto chunk_data = mesher.mesh(blocks_data, globals::BlockSize * lod, 1);
                        chunk->lod = lod;
                        chunk->mesh = std::move(chunk_data);
                        chunk->data = std::move(blocks_data);
                        chunk->color = glm::gtc::linearRand(glm::vec4(0, 0, 0, 1), glm::vec4(1, 1, 1, 1));
                        chunk->sector = sector;
                        chunk->transform = glm::gtc::translate(glm::vec3(sector) *
                            globals::ChunkSize * globals::BlockSize) * glm::gtc::scale(glm::vec3(lod));
                        chunk->dirty = true;
                        chunk->regenerate = false;
                        LOGI("re-generate chunk for sector [%d %d %d]", chunk->sector.x, chunk->sector.y, chunk->sector.z);
                        needs_update = true;
                        if (!--chunks_to_generate)
                            break;
                    }
                    else
                    {
                        chunk->lod = lod;
                        chunk->mesh = {};
                        chunk->data = {};
                        chunk->sector = sector;
                        chunk->dirty = false;
                        LOGI("skip empty chunk for sector [%d %d %d]", chunk->sector.x, chunk->sector.y, chunk->sector.z);
                    }
                }
            }
            else if (m_chunks.size() < chunk_count)
            {
                const uint32_t lod = chunk_lod(sector, cur_sector);
                auto& chunk = m_chunks.emplace_back(std::make_shared<Chunk>());
                auto blocks_data = generator.generate(sector, lod);
                if (!blocks_data.empty)
                {
                    auto chunk_data = mesher.mesh(blocks_data, globals::BlockSize * lod, 1);
                    chunk->lod = lod;
                    chunk->mesh = std::move(chunk_data);
                    chunk->data = std::move(blocks_data);
                    chunk->color = glm::gtc::linearRand(glm::vec4(0, 0, 0, 1), glm::vec4(1, 1, 1, 1));
                    chunk->sector = sector;
                    chunk->transform = glm::gtc::translate(glm::vec3(sector) *
                        globals::ChunkSize * globals::BlockSize) * glm::gtc::scale(glm::vec3(lod));
                    chunk->dirty = true;
                    chunk->regenerate = false;
                    LOGI("generate chunk for sector [%d %d %d]", chunk->sector.x, chunk->sector.y, chunk->sector.z);
                    needs_update = true;
                    if (!--chunks_to_generate)
                        break;
                }
                else
                {
                    chunk->lod = lod;
                    chunk->mesh = {};
                    chunk->data = {};
                    chunk->sector = sector;
                    chunk->dirty = false;
                    LOGI("skip empty chunk for sector [%d %d %d]", chunk->sector.x, chunk->sector.y, chunk->sector.z);
                }
            }
            else if (!chunk_indices.empty())
            {
                const uint32_t chunk_index = chunk_indices[chunk_indices_offset++];
                const uint32_t lod = chunk_lod(sector, cur_sector);
                auto& chunk = m_chunks[chunk_index];
                auto blocks_data = generator.generate(sector, lod);
                if (!blocks_data.empty)
                {
                    auto chunk_data = mesher.mesh(blocks_data, globals::BlockSize * lod, 1);
                    chunk->lod = lod;
                    chunk->mesh = std::move(chunk_data);
                    chunk->data = std::move(blocks_data);
                    chunk->color = glm::gtc::linearRand(glm::vec4(0, 0, 0, 1), glm::vec4(1, 1, 1, 1));
                    chunk->sector = sector;
                    chunk->transform = glm::gtc::translate(glm::vec3(sector) *
                        globals::ChunkSize * globals::BlockSize) * glm::gtc::scale(glm::vec3(lod));
                    chunk->dirty = true;
                    chunk->regenerate = false;
                    LOGI("re-generate chunk for sector [%d %d %d]", chunk->sector.x, chunk->sector.y, chunk->sector.z);
                    needs_update = true;
                    if (!--chunks_to_generate)
                        break;
                }
                else
                {
                    chunk->lod = lod;
                    chunk->mesh = {};
                    chunk->data = {};
                    chunk->sector = sector;
                    chunk->dirty = false;
                    LOGI("skip empty chunk for sector [%d %d %d]", chunk->sector.x, chunk->sector.y, chunk->sector.z);
                }
            }
            i_have_done_something = true;
            things_done++;
        }
        return true;
    }
    void clear_chunks() noexcept
    {
        std::lock_guard lock(m_chunks_mutex);

        for (auto& chunk : m_chunks)
        {
            systems::m_physics_system->remove_body(chunk->body_id);
            for (const auto& [layer, m] : chunk->mesh)
            {
                if (chunk->buffer[layer].alloc)
                {
                    globals::m_resources->delete_buffers.emplace(last_timeline_value,
                        std::pair(std::ref(globals::m_resources->vertex_buffer), chunk->buffer[layer]));
                }
            }
        }

        m_chunks.clear();
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

        last_timeline_value = frame.timeline_value;
        std::unique_lock lock(m_chunks_mutex, std::try_to_lock);
        if (!lock.owns_lock())
            return;

        if (!sectors_to_request.empty())
        {
            systems::m_client_system->send_message(ENET_PACKET_FLAG_RELIABLE, messages::ChunkDataMessage{
               .message_direction = messages::MessageDirection::Request,
               .sectors = sectors_to_request,
            });
            sectors_to_wait.append_range(sectors_to_request);
            sectors_to_request.clear();
        }

        std::vector<glm::ivec3> sectors_to_ready;
        for (const auto& sector : sectors_to_wait)
        {
            if (chunks_netstate[sector] == ChunkNetState::None)
            {
                chunks_netstate[sector] = ChunkNetState::Wait;
            }
            else if (chunks_netstate[sector] == ChunkNetState::Ready)
            {
                generator.deserialize_apply(sector, chunks_netdata[sector]);
                chunks_netstate[sector] = ChunkNetState::Sync;
                sectors_to_ready.emplace_back(sector);
                if (on_sector_sync)
                    on_sector_sync(sector);
            }
        }

        for (const auto& sector : sectors_to_ready)
        {
            std::erase(sectors_to_wait, sector);
        }

        const glm::ivec3 cur_sector =
            glm::floor(cam_pos / (globals::ChunkSize * globals::BlockSize));

        const uint32_t eyes = globals::xrmode ? 2 : 1;
        for (uint32_t eye = 0; eye < eyes; eye++)
        {
            const auto vp = frame.projection[eye] * frame.view[eye];
            m_frustum[eye].update(vp);
        }

        auto sorted_chunks = utils::sorted_view(m_chunks, [this](const auto& c1, const auto& c2) -> bool
        {
            const float dist1 = glm::gtx::distance2(glm::vec3(c1->sector), glm::vec3(cam_pos));
            const float dist2 = glm::gtx::distance2(glm::vec3(c2->sector), glm::vec3(cam_pos));
            return dist1 < dist2;
        });

        int polys = 0;
        std::unordered_map<BlockLayer, BatchDraw> batches;
        bool optimize_physics = false;
        for (auto& chunk : sorted_chunks)
        {
            // AABB Culling Check
            constexpr float chunk_world_size = globals::ChunkSize * globals::BlockSize;
            const glm::vec3 min_corner = glm::vec3(chunk->sector) * chunk_world_size;
            const AABB chunk_aabb{
                .min = min_corner,
                .max = min_corner + glm::vec3(chunk_world_size),
            };

            // if (!m_frustum.is_box_visible(chunk_aabb))
            //     continue; // Skip this chunk, it's not visible

            for (const auto& [layer, m] : chunk->mesh)
            {
                if (m.vertices.empty())
                {
                    if (chunk->buffer[layer].alloc)
                    {
                        globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                            std::pair(std::ref(globals::m_resources->vertex_buffer), chunk->buffer[layer]));
                    }
                    chunk->buffer.erase(layer);
                    systems::m_physics_system->remove_body(chunk->body_id);
                    continue;
                }
                if (chunk->dirty)
                {
                    LOGI("generate physics for sector [%d %d %d]", chunk->sector.x, chunk->sector.y, chunk->sector.z);
                    systems::m_physics_system->remove_body(chunk->body_id);
                    if (chunk->lod <= 1)
                    {
                        if (auto result = systems::m_physics_system->create_chunk_body(
                            globals::ChunkSize, globals::BlockSize, chunk->data, chunk->sector))
                        {
                            std::tie(chunk->body_id, chunk->shape) = result.value();
                        }
                    }

                    if (const auto sb = globals::m_resources->staging_buffer.suballoc(m.vertices.size() *
                        sizeof(shaders::SolidFlatShader::VertexInput), 64))
                    {
                        if (const auto dst_sb = globals::m_resources->vertex_buffer.suballoc(sb->size, 64))
                        {
                            std::ranges::copy(m.vertices, static_cast<shaders::SolidFlatShader::VertexInput*>(sb->ptr));
                            globals::m_resources->copy_buffers.emplace_back(globals::m_resources->vertex_buffer, *sb, dst_sb->offset);
                            //m_chunks_state.vertex_buffer = *dst_sb;

                            if (chunk->buffer[layer].alloc)
                            {
                                globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                                   std::pair(std::ref(globals::m_resources->vertex_buffer), chunk->buffer[layer]));
                            }
                            chunk->buffer[layer] = *dst_sb;
                            LOGI("generate vertex for sector [%d %d %d]", chunk->sector.x, chunk->sector.y, chunk->sector.z);
                        }
                        // defer suballocation deletion
                        globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                            std::pair(std::ref(globals::m_resources->staging_buffer), *sb));
                        if (on_sector_drawing)
                            on_sector_drawing(chunk->sector);
                    }
                    optimize_physics = true;
                }
                const auto is_visible = [&]
                {
                    for (uint32_t eye = 0; eye < eyes; eye++)
                        if (m_frustum[eye].is_box_visible(chunk_aabb))
                            return true;
                    return false;
                };
                if (is_visible())
                {
                    batches[layer].ubo.push_back(shaders::SolidFlatShader::PerObjectBuffer{
                        .ObjectTransform = glm::transpose(chunk->transform),
                        .lod = chunk->lod,
                    });
                    const auto vertex_offset = chunk->buffer[layer].offset / sizeof(shaders::SolidFlatShader::VertexInput);
                    batches[layer].draw_args.push_back({
                        .vertexCount = static_cast<uint32_t>(m.vertices.size()),
                        .instanceCount = 1,
                        .firstVertex = static_cast<uint32_t>(vertex_offset),
                        .firstInstance = 0
                    });
                    batches[layer].draw_count++;
                    polys += m.vertices.size() / 3;
                }
            }
            chunk->dirty = false;
            // chunk.mesh.clear();
        }

        if (optimize_physics)
            systems::m_physics_system->optimize();

        // LOGI("drawing %d polys", polys / 3);
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
    [[nodiscard]] static bool is_inside(const glm::ivec3& world_cell, const glm::ivec3& sector) noexcept
    {
        constexpr auto edge = globals::ChunkSize - 1;
        const glm::ivec3 local_cell = world_cell - sector * static_cast<int32_t>(globals::ChunkSize);
        return local_cell.x >= 0 && local_cell.y >= 0 && local_cell.z >= 0 &&
            local_cell.x <= edge && local_cell.y <= edge && local_cell.z <= edge;
    }
    [[nodiscard]] static bool is_edge(const glm::ivec3& world_cell, const glm::ivec3& sector) noexcept
    {
        constexpr auto edge = globals::ChunkSize - 1;
        const glm::ivec3 local_cell = world_cell - sector * static_cast<int32_t>(globals::ChunkSize);
        return local_cell.x == 0 || local_cell.y == 0 || local_cell.z == 0 ||
            local_cell.x == edge || local_cell.y == edge || local_cell.z == edge;
    }
    void regenerate_block(const glm::ivec3& sector, const glm::u8vec3& local_cell) noexcept
    {
        std::lock_guard lock(m_chunks_mutex);
        if (const auto it = std::ranges::find(m_chunks, sector, &Chunk::sector); it != m_chunks.end())
        {
            (*it)->regenerate = true;
        }
        if (is_edge(local_cell, sector))
        {
            const glm::ivec3 world_cell = glm::ivec3(local_cell) + sector * static_cast<int32_t>(globals::ChunkSize);
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                for (const int32_t i : std::to_array({-1, 1}))
                {
                    glm::ivec3 adj_cell = world_cell;
                    adj_cell[axis] += i;
                    glm::ivec3 adj_sector = sector;
                    adj_sector[axis] += i;
                    if (is_inside(adj_cell, adj_sector))
                    {
                        if (const auto it = std::ranges::find(m_chunks, adj_sector, &Chunk::sector);
                            it != m_chunks.end())
                        {
                            LOGI("regenerate edge block [%d %d %d] in sector [%d %d %d]",
                                adj_cell.x, adj_cell.y, adj_cell.z, adj_sector.x, adj_sector.y, adj_sector.z);
                            (*it)->regenerate = true;
                        }
                    }
                }
            }
        }
    }
    void break_block(const glm::vec3& origin, const glm::vec3& direction) noexcept
    {
        if (const auto hit = trace_dda(origin, direction, 10.0,
            [](const BlockType b){ return b != BlockType::Air && b != BlockType::Water; }))
        {
            const auto [world_cell, b, p, n] = hit.value();
            if (!globals::server_mode && systems::m_client_system->connected())
            {
                const messages::BlockActionMessage message{
                    .action = messages::BlockActionMessage::ActionType::Break,
                    .world_cell = world_cell
                };
                systems::m_client_system->send_message(ENET_PACKET_FLAG_RELIABLE, message);
            }
            else
            {
                break_block(world_cell);
            }
        }
    }
    void break_block(const glm::ivec3& world_cell) noexcept
    {
        const glm::ivec3 sector = glm::floor(glm::vec3(world_cell) / static_cast<float>(globals::ChunkSize));
        const glm::u8vec3 local_cell = world_cell - sector * static_cast<int32_t>(globals::ChunkSize);
        generator.remove(sector, local_cell);
        regenerate_block(sector, local_cell);
    }
    void build_block(const glm::vec3& origin, const glm::vec3& direction) noexcept
    {
        if (const auto world_cell = build_block_cell(origin, direction))
        {
            if (!globals::server_mode && systems::m_client_system->connected())
            {
                const messages::BlockActionMessage message{
                    .action = messages::BlockActionMessage::ActionType::Build,
                    .world_cell = *world_cell
                };
                systems::m_client_system->send_message(ENET_PACKET_FLAG_RELIABLE, message);
            }
            else
            {
                build_block(*world_cell);
            }
        }
    }
    void build_block(const glm::ivec3& world_cell) noexcept
    {
        const glm::ivec3 sector = glm::floor(glm::vec3(world_cell) / static_cast<float>(globals::ChunkSize));
        const glm::u8vec3 local_cell = world_cell - sector * static_cast<int32_t>(globals::ChunkSize);
        generator.edit(sector, local_cell, BlockType::Dirt);
        regenerate_block(sector, local_cell);
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
