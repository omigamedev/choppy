module;
#include <cstdint>
#include <concepts>
#include <map>
#include <vector>
#include <memory>
#include <span>
#include <unordered_map>
#include <volk.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include "Jolt/Core/Reference.h"
#include "Jolt/Physics/Collision/Shape/Shape.h"

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

export module ce.app.chunkmesh;
import ce.app.utils;
import ce.app.chunkgen;
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
    [[nodiscard]] virtual std::unordered_map<BlockType, ChunkMesh<T>> mesh(
        const ChunkData& data, const float block_size) const noexcept = 0;
};

bool operator&(Block::Mask lhs, Block::Mask rhs)
{
    return static_cast<uint8_t>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
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
class SimpleMesher final : public ChunkMesher<T>
{
public:
    [[nodiscard]] std::unordered_map<BlockType, ChunkMesh<T>> mesh(
        const ChunkData& data, const float block_size) const noexcept override
    {
        const uint32_t size = data.size;

        std::vector<glm::vec4> vertices;
        vertices.reserve(pow(size + 1, 3));
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

        std::unordered_map<BlockType, ChunkMesh<T>> meshes;
        for (uint32_t y = 0; y < size; ++y)
        {
            for (uint32_t z = 0; z < size; ++z)
            {
                for (uint32_t x = 0; x < size; ++x)
                {
                    const uint32_t idx = x + z * size + y * size * size;
                    if (data.blocks[idx].type == BlockType::Air)
                        continue;

                    auto& m = meshes[data.blocks[idx].type];

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

                    glm::vec4 CA{ 0, .5, 0, 1};
                    glm::vec4 CB{ 0,  0, 0, 1};
                    glm::vec4 CC{.5,  0, 0, 1};
                    glm::vec4 CD{.5, .5, 0, 1};

                    if (data.blocks[idx].face_mask & Block::Mask::B)
                    {
                        m.vertices.emplace_back(pack_vertex(vertices[VA], CA * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VH], CC * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VE], CB * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VA], CA * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VD], CD * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VH], CC * 63, 0));
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::F)
                    {
                        m.vertices.emplace_back(pack_vertex(vertices[VC], CA * 63, 1));
                        m.vertices.emplace_back(pack_vertex(vertices[VF], CC * 63, 1));
                        m.vertices.emplace_back(pack_vertex(vertices[VG], CB * 63, 1));
                        m.vertices.emplace_back(pack_vertex(vertices[VC], CA * 63, 1));
                        m.vertices.emplace_back(pack_vertex(vertices[VB], CD * 63, 1));
                        m.vertices.emplace_back(pack_vertex(vertices[VF], CC * 63, 1));
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::U)
                    {
                        if (data.blocks[idx].type == BlockType::Water)
                        {
                            auto CA = glm::vec4{ 0,  1, 0, 0.1f};
                            auto CB = glm::vec4{ 0, .5, 0, 0.1f};
                            auto CC = glm::vec4{.5, .5, 0, 0.1f};
                            auto CD = glm::vec4{.5,  1, 0, 0.1f};
                            m.vertices.emplace_back(pack_vertex((vertices[VE] - glm::vec4(0, .1, 0, 0)), CA * 63, 0));
                            m.vertices.emplace_back(pack_vertex((vertices[VG] - glm::vec4(0, .1, 0, 0)), CC * 63, 0));
                            m.vertices.emplace_back(pack_vertex((vertices[VF] - glm::vec4(0, .1, 0, 0)), CB * 63, 0));
                            m.vertices.emplace_back(pack_vertex((vertices[VE] - glm::vec4(0, .1, 0, 0)), CA * 63, 0));
                            m.vertices.emplace_back(pack_vertex((vertices[VH] - glm::vec4(0, .1, 0, 0)), CD * 63, 0));
                            m.vertices.emplace_back(pack_vertex((vertices[VG] - glm::vec4(0, .1, 0, 0)), CC * 63, 0));
                        }
                        else
                        {
                            auto CA = glm::vec4{.5, .5, 0, 1};
                            auto CB = glm::vec4{.5,  0, 1, 1};
                            auto CC = glm::vec4{ 1,  0, 0, 1};
                            auto CD = glm::vec4{ 1, .5, 0, 1};
                            m.vertices.emplace_back(pack_vertex(vertices[VE], CA * 63, 0));
                            m.vertices.emplace_back(pack_vertex(vertices[VG], CC * 63, 0));
                            m.vertices.emplace_back(pack_vertex(vertices[VF], CB * 63, 0));
                            m.vertices.emplace_back(pack_vertex(vertices[VE], CA * 63, 0));
                            m.vertices.emplace_back(pack_vertex(vertices[VH], CD * 63, 0));
                            m.vertices.emplace_back(pack_vertex(vertices[VG], CC * 63, 0));
                        }
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::D)
                    {
                        m.vertices.emplace_back(pack_vertex(vertices[VB], CA * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VD], CC * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VA], CB * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VB], CA * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VC], CD * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VD], CC * 63, 0));
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::R)
                    {
                        m.vertices.emplace_back(pack_vertex(vertices[VD], CA * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VG], CC * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VH], CB * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VD], CA * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VC], CD * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VG], CC * 63, 0));
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::L)
                    {
                        m.vertices.emplace_back(pack_vertex(vertices[VB], CA * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VE], CC * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VF], CB * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VB], CA * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VA], CD * 63, 0));
                        m.vertices.emplace_back(pack_vertex(vertices[VE], CC * 63, 0));
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
    std::unordered_map<BlockType, ChunkMesh<shaders::SolidFlatShader::VertexInput>> mesh;
    std::unordered_map<BlockType, vk::BufferSuballocation> buffer{};
    bool dirty = false;
    ChunkData data;
    JPH::RefConst<JPH::Shape> shape;
    JPH::BodyID body_id;
    //uint32_t size = 0;
    //uint32_t height = 0;
    //uint32_t descriptor_set_index = 0;
};
}