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

#include "Jolt/Core/Reference.h"
#include "Jolt/Physics/Collision/Shape/Shape.h"

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
/// Layout: [2b face_id | 6b v | 6b u | 6b z | 6b y | 6b x]
uint32_t pack_vertex(const glm::uvec3& pos, const glm::uvec2& uv, const uint32_t face_id)
{
    const uint32_t px = pos.x & 0x3F; // 6 bits
    const uint32_t py = pos.y & 0x3F; // 6 bits
    const uint32_t pz = pos.z & 0x3F; // 6 bits
    const uint32_t u = uv.x & 0x3F;   // 6 bits
    const uint32_t v = uv.y & 0x3F;   // 6 bits
    const uint32_t fid = face_id & 0x03; // 2 bits

    return (px) | (py << 6) | (pz << 12) | (u << 18) | (v << 24) | (fid << 30);
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
                //std::vector<glm::uvec2> points;
                //points.reserve(utils::pow(size + 1, 2));
                //for (uint32_t y = 0; y < size + 1; ++y)
                //{
                //    for (uint32_t x = 0; x < size + 1; ++x)
                //    {
                //        points.emplace_back(x, y);
                //    }
                //}
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
                            if (flip ? d > 0 : d < 0)
                            {
                                mesh.vertices.emplace_back(pack_vertex(A, CA + mat[face_index], 0));
                                mesh.vertices.emplace_back(pack_vertex(C, CC + mat[face_index], 0));
                                mesh.vertices.emplace_back(pack_vertex(B, CB + mat[face_index], 0));
                                mesh.vertices.emplace_back(pack_vertex(A, CA + mat[face_index], 0));
                                mesh.vertices.emplace_back(pack_vertex(D, CD + mat[face_index], 0));
                                mesh.vertices.emplace_back(pack_vertex(C, CC + mat[face_index], 0));
                            }
                            else
                            {
                                mesh.vertices.emplace_back(pack_vertex(A, CA + mat[face_index], 0));
                                mesh.vertices.emplace_back(pack_vertex(B, CB + mat[face_index], 0));
                                mesh.vertices.emplace_back(pack_vertex(C, CC + mat[face_index], 0));
                                mesh.vertices.emplace_back(pack_vertex(A, CA + mat[face_index], 0));
                                mesh.vertices.emplace_back(pack_vertex(C, CC + mat[face_index], 0));
                                mesh.vertices.emplace_back(pack_vertex(D, CD + mat[face_index], 0));
                            }
                        }
                    }
                }
            }
        }
        return std::move(meshes);
    }
};

template<typename T>
class SimpleMesher final : public ChunkMesher<T>
{
public:
    [[nodiscard]] std::unordered_map<BlockLayer, ChunkMesh<T>> mesh(
        const ChunkData& data, const float block_size) const noexcept override
    {
        const uint32_t size = data.size;

        std::vector<glm::vec4> vertices;
        vertices.reserve(utils::pow(size + 1, 3));
        for (uint32_t y = 0; y < size + 1; ++y)
        {
            for (uint32_t z = 0; z < size + 1; ++z)
            {
                for (uint32_t x = 0; x < size + 1; ++x)
                {
                    const float nx = static_cast<float>(x);
                    const float nz = static_cast<float>(z);
                    const float ny = static_cast<float>(y);
                    vertices.push_back({nx, ny, nz, 1.0f});
                }
            }
        }

        if (vertices.empty())
            return {};

        std::unordered_map<BlockLayer, ChunkMesh<T>> meshes;
        for (uint32_t y = 0; y < size; ++y)
        {
            for (uint32_t z = 0; z < size; ++z)
            {
                for (uint32_t x = 0; x < size; ++x)
                {
                    const uint32_t idx = x + z * size + y * size * size;
                    if (data.blocks[idx].type == BlockType::Air)
                        continue;

                    const auto& [layer, mat] = materials.at(data.blocks[idx].type);
                    auto& m = meshes[layer];

                    const uint32_t line_size = (size + 1);
                    const uint32_t plane_size = line_size * line_size;

                    const uint32_t plane_offset = y * plane_size;
                    const uint32_t next_plane_offset = (y + 1) * plane_size;
                    const uint32_t line_offset = z * line_size;

                    const uint32_t VA = plane_offset + line_offset + x;
                    const uint32_t VB = VA + line_size;
                    const uint32_t VC = VA + line_size + 1;
                    const uint32_t VD = VA + 1;
                    const uint32_t VE = next_plane_offset + line_offset + x;
                    const uint32_t VF = VE + line_size;
                    const uint32_t VG = VE + line_size + 1;
                    const uint32_t VH = VE + 1;

                    glm::uvec2 CA{0, 1};
                    glm::uvec2 CB{0, 0};
                    glm::uvec2 CC{1, 0};
                    glm::uvec2 CD{1, 1};

                    if (data.blocks[idx].face_mask & Block::Mask::U)
                    {
                        m.vertices.emplace_back(pack_vertex(vertices[VE], CA + mat[std::to_underlying(Block::FaceIndex::U)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VG], CC + mat[std::to_underlying(Block::FaceIndex::U)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VF], CB + mat[std::to_underlying(Block::FaceIndex::U)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VE], CA + mat[std::to_underlying(Block::FaceIndex::U)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VH], CD + mat[std::to_underlying(Block::FaceIndex::U)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VG], CC + mat[std::to_underlying(Block::FaceIndex::U)], 0));
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::B)
                    {
                        m.vertices.emplace_back(pack_vertex(vertices[VA], CA + mat[std::to_underlying(Block::FaceIndex::B)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VH], CC + mat[std::to_underlying(Block::FaceIndex::B)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VE], CB + mat[std::to_underlying(Block::FaceIndex::B)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VA], CA + mat[std::to_underlying(Block::FaceIndex::B)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VD], CD + mat[std::to_underlying(Block::FaceIndex::B)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VH], CC + mat[std::to_underlying(Block::FaceIndex::B)], 0));
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::F)
                    {
                        m.vertices.emplace_back(pack_vertex(vertices[VC], CA + mat[std::to_underlying(Block::FaceIndex::F)], 1));
                        m.vertices.emplace_back(pack_vertex(vertices[VF], CC + mat[std::to_underlying(Block::FaceIndex::F)], 1));
                        m.vertices.emplace_back(pack_vertex(vertices[VG], CB + mat[std::to_underlying(Block::FaceIndex::F)], 1));
                        m.vertices.emplace_back(pack_vertex(vertices[VC], CA + mat[std::to_underlying(Block::FaceIndex::F)], 1));
                        m.vertices.emplace_back(pack_vertex(vertices[VB], CD + mat[std::to_underlying(Block::FaceIndex::F)], 1));
                        m.vertices.emplace_back(pack_vertex(vertices[VF], CC + mat[std::to_underlying(Block::FaceIndex::F)], 1));
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::D)
                    {
                        m.vertices.emplace_back(pack_vertex(vertices[VB], CA + mat[std::to_underlying(Block::FaceIndex::D)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VD], CC + mat[std::to_underlying(Block::FaceIndex::D)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VA], CB + mat[std::to_underlying(Block::FaceIndex::D)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VB], CA + mat[std::to_underlying(Block::FaceIndex::D)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VC], CD + mat[std::to_underlying(Block::FaceIndex::D)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VD], CC + mat[std::to_underlying(Block::FaceIndex::D)], 0));
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::R)
                    {
                        m.vertices.emplace_back(pack_vertex(vertices[VD], CA + mat[std::to_underlying(Block::FaceIndex::R)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VG], CC + mat[std::to_underlying(Block::FaceIndex::R)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VH], CB + mat[std::to_underlying(Block::FaceIndex::R)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VD], CA + mat[std::to_underlying(Block::FaceIndex::R)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VC], CD + mat[std::to_underlying(Block::FaceIndex::R)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VG], CC + mat[std::to_underlying(Block::FaceIndex::R)], 0));
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::L)
                    {
                        m.vertices.emplace_back(pack_vertex(vertices[VB], CA + mat[std::to_underlying(Block::FaceIndex::L)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VE], CC + mat[std::to_underlying(Block::FaceIndex::L)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VF], CB + mat[std::to_underlying(Block::FaceIndex::L)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VB], CA + mat[std::to_underlying(Block::FaceIndex::L)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VA], CD + mat[std::to_underlying(Block::FaceIndex::L)], 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VE], CC + mat[std::to_underlying(Block::FaceIndex::L)], 0));
                    }
                }
            }
        }
        return std::move(meshes);
    }
};

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
    //uint32_t size = 0;
    //uint32_t height = 0;
    //uint32_t descriptor_set_index = 0;
};
}