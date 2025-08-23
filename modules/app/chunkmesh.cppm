module;
#include <cstdint>
#include <concepts>
#include <vector>
#include <memory>
#include <span>
#include <volk.h>

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
import glm;

export namespace ce::app
{
template<typename T>
concept VertexType = requires(T t)
{
    { t.position } -> std::same_as<glm::vec4&>;
};

template<VertexType T>
struct ChunkMesh
{
    using VertexType = T;
    std::vector<uint32_t> indices;
    std::vector<VertexType> vertices;
};
template<VertexType T>
class ChunkMesher
{
public:
    virtual ~ChunkMesher() = default;
    [[nodiscard]] virtual ChunkMesh<T> mesh(const ChunkData& data) const noexcept = 0;
};

bool operator&(Block::Mask lhs, Block::Mask rhs)
{
    return static_cast<uint8_t>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
}

template<VertexType T>
class SimpleMesher final : public ChunkMesher<T>
{
public:
    [[nodiscard]] ChunkMesh<T> mesh(const ChunkData& data) const noexcept override
    {
        const uint32_t size = data.size;

        std::vector<T> vertices;
        vertices.reserve(pow(size + 1, 3));
        const float normalizer = 1.f / static_cast<float>(size);
        for (uint32_t y = 0; y < size + 1; ++y)
        {
            for (uint32_t z = 0; z < size + 1; ++z)
            {
                for (uint32_t x = 0; x < size + 1; ++x)
                {
                    const float nx = static_cast<float>(x) * normalizer;
                    const float nz = static_cast<float>(z) * normalizer;
                    const float ny = static_cast<float>(y) * normalizer;
                    vertices.push_back({
                        .position = {nx, ny, -nz, 1.0f},
                        .color = {x, y, z, 1.0f},
                    });
                }
            }
        }

        if (vertices.empty())
            return {};

        ChunkMesh<T> m;
        m.vertices.reserve(36 * pow(size, 3));
        for (uint32_t y = 0; y < size; ++y)
        {
            for (uint32_t z = 0; z < size; ++z)
            {
                for (uint32_t x = 0; x < size; ++x)
                {
                    const uint32_t idx = x + z * size + y * size * size;
                    if (data.blocks[idx].type != BlockType::Dirt)
                        continue;

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

                    const glm::vec4 CA{0, 0, 0, 1};
                    const glm::vec4 CB{0, 0, 1, 1};
                    const glm::vec4 CC{0, 1, 0, 1};
                    const glm::vec4 CD{1, 0, 0, 1};

                    if (data.blocks[idx].face_mask & Block::Mask::B)
                    {
                        m.vertices.emplace_back(vertices[VA].position, CA);
                        m.vertices.emplace_back(vertices[VE].position, CB);
                        m.vertices.emplace_back(vertices[VH].position, CC);
                        m.vertices.emplace_back(vertices[VA].position, CA);
                        m.vertices.emplace_back(vertices[VH].position, CC);
                        m.vertices.emplace_back(vertices[VD].position, CD);
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::F)
                    {
                        m.vertices.emplace_back(vertices[VC].position, CA);
                        m.vertices.emplace_back(vertices[VG].position, CB);
                        m.vertices.emplace_back(vertices[VF].position, CC);
                        m.vertices.emplace_back(vertices[VC].position, CA);
                        m.vertices.emplace_back(vertices[VF].position, CC);
                        m.vertices.emplace_back(vertices[VB].position, CD);
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::U)
                    {
                        m.vertices.emplace_back(vertices[VE].position, CA);
                        m.vertices.emplace_back(vertices[VF].position, CB);
                        m.vertices.emplace_back(vertices[VG].position, CC);
                        m.vertices.emplace_back(vertices[VE].position, CA);
                        m.vertices.emplace_back(vertices[VG].position, CC);
                        m.vertices.emplace_back(vertices[VH].position, CD);
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::D)
                    {
                        m.vertices.emplace_back(vertices[VB].position, CA);
                        m.vertices.emplace_back(vertices[VA].position, CB);
                        m.vertices.emplace_back(vertices[VD].position, CC);
                        m.vertices.emplace_back(vertices[VB].position, CA);
                        m.vertices.emplace_back(vertices[VD].position, CC);
                        m.vertices.emplace_back(vertices[VC].position, CD);
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::R)
                    {
                        m.vertices.emplace_back(vertices[VD].position, CA);
                        m.vertices.emplace_back(vertices[VH].position, CB);
                        m.vertices.emplace_back(vertices[VG].position, CC);
                        m.vertices.emplace_back(vertices[VD].position, CA);
                        m.vertices.emplace_back(vertices[VG].position, CC);
                        m.vertices.emplace_back(vertices[VC].position, CD);
                    }
                    if (data.blocks[idx].face_mask & Block::Mask::L)
                    {
                        m.vertices.emplace_back(vertices[VB].position, CA);
                        m.vertices.emplace_back(vertices[VF].position, CB);
                        m.vertices.emplace_back(vertices[VE].position, CC);
                        m.vertices.emplace_back(vertices[VB].position, CA);
                        m.vertices.emplace_back(vertices[VE].position, CC);
                        m.vertices.emplace_back(vertices[VA].position, CD);
                    }
                }
            }
        }
        return m;
    }
};

class Chunk final : NoCopy
{
    using Shader = shaders::SolidFlatShader;
    std::shared_ptr<vk::Buffer> m_vertex_buffer;
    glm::mat4 m_transform{};
    glm::vec4 m_color{};
    glm::ivec3 m_sector{};
    uint32_t m_vertex_count = 0;
    uint32_t m_size = 0;
    uint32_t m_height = 0;
    uint32_t m_descriptor_set_index = 0;
public:
    [[nodiscard]] const auto& vertex_buffer() const noexcept { return m_vertex_buffer; }
    [[nodiscard]] uint32_t vertex_count() const noexcept { return m_vertex_count; }
    [[nodiscard]] const glm::mat4& transform() const noexcept { return m_transform; }
    [[nodiscard]] uint32_t descriptor_set_index() const noexcept { return m_descriptor_set_index; }
    [[nodiscard]] const glm::vec4& color() const noexcept { return m_color; }
    [[nodiscard]] const glm::ivec3& sector() const noexcept { return m_sector; }
    bool create(const std::shared_ptr<vk::Context>& vk, const ChunkMesh<shaders::SolidFlatShader::VertexInput>& mesh,
        const glm::ivec3& sector, const glm::vec4& color, uint32_t set_index) noexcept
    {
        m_descriptor_set_index = set_index;
        m_transform = glm::gtc::translate(glm::vec3(sector));
        m_vertex_count = static_cast<uint32_t>(mesh.vertices.size());
        m_color = color;
        m_sector = sector;

        if (m_vertex_count == 0)
            return false;

        // Create and upload vertex buffer
        m_vertex_buffer = std::make_shared<vk::Buffer>(vk, "CubeVertexBuffer");
        if (!m_vertex_buffer->create(mesh.vertices.size() * sizeof(shaders::SolidFlatShader::VertexInput),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
            !m_vertex_buffer->create_staging(mesh.vertices.size() * sizeof(shaders::SolidFlatShader::VertexInput)))
        {
            LOGE("Failed to create cube vertex buffer");
            return false;
        }

        // Upload data to GPU and destroy staging buffers
        vk->exec_immediate("create_cube", [this, &mesh](VkCommandBuffer cmd){
            m_vertex_buffer->update_cmd(cmd, std::span(mesh.vertices));
        });
        m_vertex_buffer->destroy_staging();
        return true;
    }
};
}