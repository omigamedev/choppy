module;
#include <cstdint>
#include <concepts>
#include <map>
#include <vector>
#include <memory>
#include <span>
#include <unordered_map>
#include <volk.h>

#include "glm/gtx/compatibility.hpp"

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

template<VertexType T>
struct ChunkMesh final
{
    using VertexType = T;
    std::vector<VertexType> vertices;
};
template<VertexType T>
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

template<VertexType T>
class SimpleMesher final : public ChunkMesher<T>
{
public:
    [[nodiscard]] std::unordered_map<BlockType, ChunkMesh<T>> mesh(
        const ChunkData& data, const float block_size) const noexcept override
    {
        const uint32_t size = data.size;

        std::vector<T> vertices;
        vertices.reserve(pow(size + 1, 3));
        for (uint32_t y = 0; y < size + 1; ++y)
        {
            for (uint32_t z = 0; z < size + 1; ++z)
            {
                for (uint32_t x = 0; x < size + 1; ++x)
                {
                    const float nx = static_cast<float>(x) * block_size;
                    const float nz = static_cast<float>(z) * block_size;
                    const float ny = static_cast<float>(y) * block_size;
                    vertices.push_back({
                        .position = {nx, ny, nz, 1.0f},
                        .color = {x, y, z, 1.0f},
                    });
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
                        m.vertices.emplace_back(vertices[VA].position, CA);
                        m.vertices.emplace_back(vertices[VH].position, CC);
                        m.vertices.emplace_back(vertices[VE].position, CB);
                        m.vertices.emplace_back(vertices[VA].position, CA);
                        m.vertices.emplace_back(vertices[VD].position, CD);
                        m.vertices.emplace_back(vertices[VH].position, CC);
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::F)
                    {
                        m.vertices.emplace_back(vertices[VC].position, CA);
                        m.vertices.emplace_back(vertices[VF].position, CC);
                        m.vertices.emplace_back(vertices[VG].position, CB);
                        m.vertices.emplace_back(vertices[VC].position, CA);
                        m.vertices.emplace_back(vertices[VB].position, CD);
                        m.vertices.emplace_back(vertices[VF].position, CC);
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::U)
                    {
                        if (data.blocks[idx].type == BlockType::Water)
                        {
                            auto CA = glm::vec4{ 0,  1, 0, 0.5f};
                            auto CB = glm::vec4{ 0, .5, 0, 0.5f};
                            auto CC = glm::vec4{.5, .5, 0, 0.5f};
                            auto CD = glm::vec4{.5,  1, 0, 0.5f};
                            m.vertices.emplace_back(vertices[VE].position - glm::vec4(0, .1, 0, 0), CA);
                            m.vertices.emplace_back(vertices[VG].position - glm::vec4(0, .1, 0, 0), CC);
                            m.vertices.emplace_back(vertices[VF].position - glm::vec4(0, .1, 0, 0), CB);
                            m.vertices.emplace_back(vertices[VE].position - glm::vec4(0, .1, 0, 0), CA);
                            m.vertices.emplace_back(vertices[VH].position - glm::vec4(0, .1, 0, 0), CD);
                            m.vertices.emplace_back(vertices[VG].position - glm::vec4(0, .1, 0, 0), CC);
                        }
                        else
                        {
                            auto CA = glm::vec4{.5, .5, 0, 1};
                            auto CB = glm::vec4{.5,  0, 1, 1};
                            auto CC = glm::vec4{ 1,  0, 0, 1};
                            auto CD = glm::vec4{ 1, .5, 0, 1};
                            m.vertices.emplace_back(vertices[VE].position, CA);
                            m.vertices.emplace_back(vertices[VG].position, CC);
                            m.vertices.emplace_back(vertices[VF].position, CB);
                            m.vertices.emplace_back(vertices[VE].position, CA);
                            m.vertices.emplace_back(vertices[VH].position, CD);
                            m.vertices.emplace_back(vertices[VG].position, CC);
                        }
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::D)
                    {
                        m.vertices.emplace_back(vertices[VB].position, CA);
                        m.vertices.emplace_back(vertices[VD].position, CC);
                        m.vertices.emplace_back(vertices[VA].position, CB);
                        m.vertices.emplace_back(vertices[VB].position, CA);
                        m.vertices.emplace_back(vertices[VC].position, CD);
                        m.vertices.emplace_back(vertices[VD].position, CC);
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::R)
                    {
                        m.vertices.emplace_back(vertices[VD].position, CA);
                        m.vertices.emplace_back(vertices[VG].position, CC);
                        m.vertices.emplace_back(vertices[VH].position, CB);
                        m.vertices.emplace_back(vertices[VD].position, CA);
                        m.vertices.emplace_back(vertices[VC].position, CD);
                        m.vertices.emplace_back(vertices[VG].position, CC);
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::L)
                    {
                        m.vertices.emplace_back(vertices[VB].position, CA);
                        m.vertices.emplace_back(vertices[VE].position, CC);
                        m.vertices.emplace_back(vertices[VF].position, CB);
                        m.vertices.emplace_back(vertices[VB].position, CA);
                        m.vertices.emplace_back(vertices[VA].position, CD);
                        m.vertices.emplace_back(vertices[VE].position, CC);
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
    //uint32_t size = 0;
    //uint32_t height = 0;
    //uint32_t descriptor_set_index = 0;
};
}