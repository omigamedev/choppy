module;
#include <cstdio>
#include <concepts>
#include <map>
#include <vector>
#include <memory>
#include <span>
#include <array>
#include <unordered_map>
#include <volk.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#elifdef _WIN32
#define LOGE(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

export module ce.app:chunkmesh;
import :utils;
import :chunkgen;
import ce.shaders.solidflat;
import ce.vk;
import ce.vk.buffer;
import glm;

export namespace ce::app
{
template<typename T>
concept VertexType = requires(T t)
{
    { t.position } -> std::same_as<glm::vec4&>;
};

template<typename T>
struct ChunkMesh final
{
    using VertexType = T;
    std::vector<VertexType> vertices;
};
template<typename T>
class ChunkMesher
{
public:
    virtual ~ChunkMesher() = default;
    [[nodiscard]] virtual std::unordered_map<BlockLayer, ChunkMesh<T>> mesh(
        const ChunkData& data, const float block_size) const noexcept = 0;
};

bool operator&(const uint8_t lhs, const Block::Mask rhs)
{
    return lhs & static_cast<uint8_t>(rhs);
}

/// @brief Packs vertex data into a single 32-bit integer.
/// Layout: [1b v | 1b u | 12b layer | 6b z | 6b y | 6b x]
uint32_t pack_vertex(const glm::uvec3& pos, const glm::uvec2& uv, const glm::uvec2& grid)
{
    const uint32_t layer = grid.y * 2 + grid.x;
    const uint32_t px = pos.x & 0x3F; //  6 bits - off 0
    const uint32_t py = pos.y & 0x3F; //  6 bits - off 6
    const uint32_t pz = pos.z & 0x3F; //  6 bits - off 12
    const uint32_t l = layer & 0xFFF; // 12 bits - off 18
    const uint32_t u = uv.x & 0x01;   //  1 bit  - off 30
    const uint32_t v = uv.y & 0x01;   //  1 bit  - off 31

    return (px) | (py << 6) | (pz << 12) | (l << 18) | (u << 30) | (v << 31);
}

/// @brief Packs vertex data into a single 32-bit integer.
/// Layout: [27b free | 4b occlusion | 3b face]
uint32_t pack_vertex_ext(const uint32_t face_id, const uint32_t occlusion)
{
    const uint32_t face = face_id & 0x07;
    const uint32_t occ = occlusion & 0x0F;
    return (face) | (occ << 3);
}

template<typename T>
class GreedyMesher final : public ChunkMesher<T>
{
public:
    [[nodiscard]] std::unordered_map<BlockLayer, ChunkMesh<T>> mesh(
        const ChunkData& data, const float block_size) const noexcept override
    {
        struct Slice
        {
            Block::Mask mask;
            // uses glm::vec3[i] component index
            int8_t main_comp;
            int8_t dir;
            int8_t u_comp;
            int8_t v_comp;
            bool flip;
        };
        const auto slices = std::to_array<Slice>({
            {Block::Mask::U, 1, -1, 0, 2, false},
            {Block::Mask::D, 1,  1, 0, 2, false},
            {Block::Mask::F, 2, -1, 0, 1, true},
            {Block::Mask::B, 2,  1, 0, 1, true},
            {Block::Mask::L, 0,  1, 2, 1, false},
            {Block::Mask::R, 0, -1, 2, 1, false}
        });

        const uint32_t size = data.size;
        std::unordered_map<BlockLayer, ChunkMesh<T>> meshes;
        for (int face_index = 0; face_index < slices.size(); ++face_index)
        {
            const auto& [m, sc, d, uc, vc, flip] = slices[face_index];
            for (uint32_t slice = 0; slice < size; ++slice)
            {
                for (uint32_t y = 0; y < size; ++y)
                {
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        auto slice_cell = d < 0 ? (size-1)-slice : slice;
                        const auto to_cell = [sc, uc, vc, slice_cell](const glm::uvec2& pos)
                        {
                            glm::uvec3 v;
                            v[sc] = slice_cell;
                            v[uc] = pos.x;
                            v[vc] = pos.y;
                            return v;
                        };
                        auto slice_plane = d < 0 ? (size)-slice : slice;
                        const auto to_plane = [sc, uc, vc, slice_plane](const glm::uvec2& pos)
                        {
                            glm::uvec3 v;
                            v[sc] = slice_plane;
                            v[uc] = pos.x;
                            v[vc] = pos.y;
                            return v;
                        };
                        const glm::uvec3 cell = to_cell({x, y});
                        const uint32_t idx = cell.x + cell.z * size + cell.y * size * size;
                        if (data.blocks[idx].type == BlockType::Air)
                            continue;
                        const auto& [layer, mat] = materials.at(data.blocks[idx].type);
                        auto& mesh = meshes[layer];
                        const auto A = to_plane(glm::uvec2(x, y));
                        const auto B = to_plane(glm::uvec2(x, y+1));
                        const auto C = to_plane(glm::uvec2(x+1, y+1));
                        const auto D = to_plane(glm::uvec2(x+1, y));
                        constexpr auto CA = glm::uvec2{0, 1};
                        constexpr auto CB = glm::uvec2{0, 0};
                        constexpr auto CC = glm::uvec2{1, 0};
                        constexpr auto CD = glm::uvec2{1, 1};
                        if (data.blocks[idx].face_mask & m)
                        {
                            const auto world_cell = data.sector * static_cast<int32_t>(data.size) + glm::ivec3(cell);
                            const uint32_t water_depth = std::max<int32_t>(0, 7 + world_cell.y);
                            const uint32_t occ = data.blocks[idx].water_mask > 0 ? water_depth : 7;
                            if (flip ? d > 0 : d < 0)
                            {
                                mesh.vertices.emplace_back(pack_vertex(A, CA, mat[face_index]), pack_vertex_ext(face_index, occ));
                                mesh.vertices.emplace_back(pack_vertex(C, CC, mat[face_index]), pack_vertex_ext(face_index, occ));
                                mesh.vertices.emplace_back(pack_vertex(B, CB, mat[face_index]), pack_vertex_ext(face_index, occ));
                                mesh.vertices.emplace_back(pack_vertex(A, CA, mat[face_index]), pack_vertex_ext(face_index, occ));
                                mesh.vertices.emplace_back(pack_vertex(D, CD, mat[face_index]), pack_vertex_ext(face_index, occ));
                                mesh.vertices.emplace_back(pack_vertex(C, CC, mat[face_index]), pack_vertex_ext(face_index, occ));
                            }
                            else
                            {
                                mesh.vertices.emplace_back(pack_vertex(A, CA, mat[face_index]), pack_vertex_ext(face_index, occ));
                                mesh.vertices.emplace_back(pack_vertex(B, CB, mat[face_index]), pack_vertex_ext(face_index, occ));
                                mesh.vertices.emplace_back(pack_vertex(C, CC, mat[face_index]), pack_vertex_ext(face_index, occ));
                                mesh.vertices.emplace_back(pack_vertex(A, CA, mat[face_index]), pack_vertex_ext(face_index, occ));
                                mesh.vertices.emplace_back(pack_vertex(C, CC, mat[face_index]), pack_vertex_ext(face_index, occ));
                                mesh.vertices.emplace_back(pack_vertex(D, CD, mat[face_index]), pack_vertex_ext(face_index, occ));
                            }
                        }
                    }
                }
            }
        }
        return std::move(meshes);
    }
};
}
