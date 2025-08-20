module;
#include <array>
#include <span>
#include <vector>
#include <memory>
#include <concepts>
#include <deque>
#include <volk.h>
#include <PerlinNoise.hpp>

#include "vk_mem_alloc.h"
#include "glm/gtx/compatibility.hpp"
// #include "glm/gtx/euler_angles.hpp"
// #include "glm/gtx/quaternion.hpp"

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
import glm;

export namespace ce::app
{
class Grid final
{
    std::shared_ptr<vk::Buffer> m_vertex_buffer;
    std::shared_ptr<vk::Buffer> m_index_buffer;
    uint32_t m_index_count = 0;

public:
    [[nodiscard]] const auto& vertex_buffer() const noexcept { return m_vertex_buffer; }
    [[nodiscard]] const auto& index_buffer() const noexcept { return m_index_buffer; }
    [[nodiscard]] uint32_t index_count() const noexcept { return m_index_count; }

    bool create(const std::shared_ptr<vk::Context>& vk, uint32_t size) noexcept
    {
        // Define the 8 vertices of the cube with unique colors for interpolation
        constexpr std::array<shaders::SolidFlatShader::VertexInput, 4> vertices = {{
            {{0, -2, 0, 1}, {0, 0, 0, 1}},
            {{0, -2, -1, 1}, {0, 0, 1, 1}},
            {{1, -2, -1, 1}, {1, 0, 1, 1}},
            {{1, -2, 0, 1}, {1, 0, 0, 1}},
        }};

        // Define the 36 indices for the 12 triangles of the cube
        constexpr std::array<uint32_t, 6> indices = {
            0, 1, 2,
            0, 2, 3,
        };
        m_index_count = static_cast<uint32_t>(indices.size());

        // Create and upload vertex buffer
        m_vertex_buffer = std::make_shared<vk::Buffer>(vk, "GridVertexBuffer");
        if (!m_vertex_buffer->create(vertices.size() * sizeof(shaders::SolidFlatShader::VertexInput),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
            !m_vertex_buffer->create_staging(vertices.size() * sizeof(shaders::SolidFlatShader::VertexInput)))
        {
            LOGE("Failed to create cube vertex buffer");
            return false;
        }

        // Create and upload index buffer
        m_index_buffer = std::make_shared<vk::Buffer>(vk, "GridIndexBuffer");
        if (!m_index_buffer->create(indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT) ||
            !m_index_buffer->create_staging(indices.size() * sizeof(uint32_t)))
        {
            LOGE("Failed to create cube index buffer");
            return false;
        }

        // Upload data to GPU and destroy staging buffers
        vk->exec_immediate("create_grid", [this, &vertices, &indices](VkCommandBuffer cmd){
            m_vertex_buffer->update_cmd(cmd, vertices);
            m_index_buffer->update_cmd(cmd, indices);
        });
        m_vertex_buffer->destroy_staging();
        m_index_buffer->destroy_staging();

        return true;
    }
};

class Cube final
{
    std::shared_ptr<vk::Buffer> m_vertex_buffer;
    std::shared_ptr<vk::Buffer> m_index_buffer;
    uint32_t m_index_count = 0;

public:
    [[nodiscard]] const auto& vertex_buffer() const noexcept { return m_vertex_buffer; }
    [[nodiscard]] const auto& index_buffer() const noexcept { return m_index_buffer; }
    [[nodiscard]] uint32_t index_count() const noexcept { return m_index_count; }

    bool create(const std::shared_ptr<vk::Context>& vk) noexcept
    {
        // Define the 8 vertices of the cube with unique colors for interpolation
        constexpr std::array<shaders::SolidFlatShader::VertexInput, 8> vertices = {{
            // {position},                {color}
            {{-0.5f, -0.5f,  0.5f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}}, // 0: Front-Bottom-Left,  Red
            {{ 0.5f, -0.5f,  0.5f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}}, // 1: Front-Bottom-Right, Green
            {{ 0.5f,  0.5f,  0.5f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}}, // 2: Front-Top-Right,    Blue
            {{-0.5f,  0.5f,  0.5f, 1.0f}, {1.0f, 1.0f, 0.0f, 1.0f}}, // 3: Front-Top-Left,     Yellow
            {{-0.5f, -0.5f, -0.5f, 1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}}, // 4: Back-Bottom-Left,   Magenta
            {{ 0.5f, -0.5f, -0.5f, 1.0f}, {0.0f, 1.0f, 1.0f, 1.0f}}, // 5: Back-Bottom-Right,  Cyan
            {{ 0.5f,  0.5f, -0.5f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}, // 6: Back-Top-Right,     White
            {{-0.5f,  0.5f, -0.5f, 1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}}, // 7: Back-Top-Left,      Gray
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
        m_index_count = static_cast<uint32_t>(indices.size());

        // Create and upload vertex buffer
        m_vertex_buffer = std::make_shared<vk::Buffer>(vk, "CubeVertexBuffer");
        if (!m_vertex_buffer->create(vertices.size() * sizeof(shaders::SolidFlatShader::VertexInput),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
            !m_vertex_buffer->create_staging(vertices.size() * sizeof(shaders::SolidFlatShader::VertexInput)))
        {
            LOGE("Failed to create cube vertex buffer");
            return false;
        }

        // Create and upload index buffer
        m_index_buffer = std::make_shared<vk::Buffer>(vk, "CubeIndexBuffer");
        if (!m_index_buffer->create(indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT) ||
            !m_index_buffer->create_staging(indices.size() * sizeof(uint32_t)))
        {
            LOGE("Failed to create cube index buffer");
            return false;
        }

        // Upload data to GPU and destroy staging buffers
        vk->exec_immediate("create_cube", [this, &vertices, &indices](VkCommandBuffer cmd){
            m_vertex_buffer->update_cmd(cmd, vertices);
            m_index_buffer->update_cmd(cmd, indices);
        });
        m_vertex_buffer->destroy_staging();
        m_index_buffer->destroy_staging();

        return true;
    }
};

constexpr int32_t pow(const int32_t base, const uint32_t exp)
{
    return (exp == 0) ? 1 :
           (exp % 2 == 0) ? pow(base * base, exp / 2) :
                            base * pow(base, exp - 1);
}

struct NoCopy
{
    NoCopy() = default;
    NoCopy(const NoCopy& other) = delete;
    NoCopy(NoCopy&& other) noexcept = default;
    NoCopy& operator=(const NoCopy& other) = delete;
    NoCopy& operator=(NoCopy&& other) noexcept = default;
};

// Generators
enum class BlockType : uint8_t
{
    Air,
    Water,
    Grass,
    Dirt,
};
struct Block final
{
    BlockType type;
};
struct ChunkData final : NoCopy
{
    ChunkData(const uint32_t size, const glm::ivec3& sector, const std::vector<Block>&& blocks)
        : size(size),
          sector(sector),
          blocks(blocks)
    {
    }

    uint32_t size;
    glm::ivec3 sector;
    std::vector<Block> blocks;
};
class ChunkGenerator
{
public:
    virtual ~ChunkGenerator() = default;
    [[nodiscard]] virtual ChunkData generate(glm::ivec3 sector) const noexcept = 0;
};
class FlatGenerator final : public ChunkGenerator
{
    uint32_t m_size = 0;
    uint32_t m_ground_height = 0;
public:
    explicit FlatGenerator(const uint32_t size, const uint32_t ground_height) noexcept
        : m_size(size), m_ground_height(ground_height) { }
    [[nodiscard]] ChunkData generate([[maybe_unused]] const glm::ivec3 sector) const noexcept override
    {
        const int32_t ssz = static_cast<int32_t>(m_size);
        std::vector<BlockType> tmp;
        tmp.reserve(pow(ssz + 2, 3));
        for (int32_t y = -1; y < ssz + 1; ++y)
        {
            for (int32_t z = -1; z < ssz + 1; ++z)
            {
                for (int32_t x = -1; x < ssz + 1; ++x)
                {
                    const glm::ivec3 loc{x, y, z};
                    const glm::ivec3 cell = loc + sector * ssz;
                    const glm::vec3 nc = glm::vec3(cell) / static_cast<float>(ssz);
                    const float terrain_height = cosf(nc.x * 2.f) * sinf(nc.z * 5.f) * 10.f;
                    const BlockType block = cell.y <= terrain_height ? BlockType::Dirt : BlockType::Air;
                    tmp.emplace_back(block);
                }
            }
        }

        std::vector<Block> blocks;
        blocks.reserve(pow(ssz, 3));
        for (uint32_t y = 0; y < m_size; ++y)
        {
            for (uint32_t z = 0; z < m_size; ++z)
            {
                for (uint32_t x = 0; x < m_size; ++x)
                {
                    const int32_t sz = static_cast<int32_t>(m_size + 2);
                    const auto C = tmp[(y + 1) * pow(sz, 2) + (z + 1) * sz + x + 1];
                    const auto U = tmp[(y + 2) * pow(sz, 2) + (z + 1) * sz + x + 1] == BlockType::Air;
                    const auto D = tmp[(y + 0) * pow(sz, 2) + (z + 1) * sz + x + 1] == BlockType::Air;
                    const auto F = tmp[(y + 1) * pow(sz, 2) + (z + 2) * sz + x + 1] == BlockType::Air;
                    const auto B = tmp[(y + 1) * pow(sz, 2) + (z + 0) * sz + x + 1] == BlockType::Air;
                    const auto R = tmp[(y + 1) * pow(sz, 2) + (z + 1) * sz + x + 2] == BlockType::Air;
                    const auto L = tmp[(y + 1) * pow(sz, 2) + (z + 1) * sz + x + 0] == BlockType::Air;
                    blocks.emplace_back((U || D || F || B || R || L) ? C : BlockType::Air);
                    //blocks.emplace_back(C);
                }
            }
        }

        return ChunkData{m_size, sector, std::move(blocks)};
    }
};

// Mesher
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

template<VertexType T>
class SimpleMesher final : public ChunkMesher<T>
{
public:
    [[nodiscard]] ChunkMesh<T> mesh(const ChunkData& data) const noexcept override
    {
        const uint32_t size = data.size;
        ChunkMesh<T> m;
        m.vertices.reserve((size + 1) * (size + 1) * (size + 1));

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
                    m.vertices.push_back({
                        .position = {nx, ny, -nz, 1.0f},
                        .color = {x, y, z, 1.0f},
                    });
                }
            }
        }
        m.indices.reserve(size * size * size);

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
                    //const uint32_t next_line_offset = (z + 1) * line_size;

                    const uint32_t A = plane_offset + line_offset + x;
                    const uint32_t B = A + line_size;
                    const uint32_t C = A + line_size + 1;
                    const uint32_t D = A + 1;
                    const uint32_t E = next_plane_offset + line_offset + x;
                    const uint32_t F = E + line_size;
                    const uint32_t G = E + line_size + 1;
                    const uint32_t H = E + 1;
                    const std::array cube_faces = {
                        // Front face (+Z)
                        A, E, H,   A, H, D,
                        // Back face (-Z)
                        C, G, F,   C, F, B,
                        // Top face (+Y)
                        E, F, H,   H, F, G,
                        // Bottom face (-Y)
                        B, A, D,   B, D, C,
                        // Right face (+X)
                        D, H, C,   C, H, G,
                        // Left face (-X)
                        B, F, E,   B, E, A,
                    };
                    m.indices.append_range(cube_faces);
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
    std::shared_ptr<vk::Buffer> m_index_buffer;
    glm::mat4 m_transform{};
    glm::vec4 m_color{};
    glm::ivec3 m_sector{};
    uint32_t m_index_count = 0;
    uint32_t m_size = 0;
    uint32_t m_height = 0;
    uint32_t m_descriptor_set_index = 0;
public:
    [[nodiscard]] const auto& vertex_buffer() const noexcept { return m_vertex_buffer; }
    [[nodiscard]] const auto& index_buffer() const noexcept { return m_index_buffer; }
    [[nodiscard]] uint32_t index_count() const noexcept { return m_index_count; }
    [[nodiscard]] const glm::mat4& transform() const noexcept { return m_transform; }
    [[nodiscard]] uint32_t descriptor_set_index() const noexcept { return m_descriptor_set_index; }
    [[nodiscard]] const glm::vec4& color() const noexcept { return m_color; }
    [[nodiscard]] const glm::ivec3& sector() const noexcept { return m_sector; }
    bool create(const std::shared_ptr<vk::Context>& vk, const ChunkMesh<shaders::SolidFlatShader::VertexInput>& mesh,
        const glm::ivec3& sector, const glm::vec4& color, uint32_t set_index) noexcept
    {
        m_descriptor_set_index = set_index;
        m_transform = glm::gtc::translate(glm::vec3(sector));
        m_index_count = static_cast<uint32_t>(mesh.indices.size());
        m_color = color;
        m_sector = sector;

        if (m_index_count == 0)
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

        // Create and upload index buffer
        m_index_buffer = std::make_shared<vk::Buffer>(vk, "CubeIndexBuffer");
        if (!m_index_buffer->create(mesh.indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT) ||
            !m_index_buffer->create_staging(mesh.indices.size() * sizeof(uint32_t)))
        {
            LOGE("Failed to create cube index buffer");
            return false;
        }

        // Upload data to GPU and destroy staging buffers
        vk->exec_immediate("create_cube", [this, &mesh](VkCommandBuffer cmd){
            m_vertex_buffer->update_cmd(cmd, std::span(mesh.vertices));
            m_index_buffer->update_cmd(cmd, std::span(mesh.indices));
        });
        m_vertex_buffer->destroy_staging();
        m_index_buffer->destroy_staging();
        return true;
    }
};
enum class GamepadButton : uint8_t
{
    Menu, View,
    A, B, X, Y,
    Up, Down, Left, Right,
    ShoulderLeft, ShoulderRight,
    ThumbLeft, ThumbRight,
    EnumCount
};
struct GamepadState
{
    std::array<bool, static_cast<size_t>(GamepadButton::EnumCount)> buttons{false};
    float thumbstick_left[2]{};
    float thumbstick_right[2]{};
    float trigger_left{0};
    float trigger_right{0};
};
class AppBase final
{
    std::shared_ptr<xr::Context> m_xr;
    std::shared_ptr<vk::Context> m_vk;
    std::shared_ptr<shaders::SolidFlatShader> solid_flat;
    std::shared_ptr<vk::Buffer> m_vertex_buffer;
    std::vector<shaders::SolidFlatShader::VertexInput> vertices;
    shaders::SolidFlatShader::PerFrameConstants uniform{};
	siv::PerlinNoise perlin{ std::random_device{} };
    std::shared_ptr<Grid> m_grid;
    std::vector<Chunk> m_chunks;
    VkRenderPass m_renderpass = VK_NULL_HANDLE;
    VkSampleCountFlagBits m_sample_count = VK_SAMPLE_COUNT_1_BIT;
    glm::ivec2 m_size{0, 0};
    bool dragging = false;
    glm::ivec2 drag_start = { 0, 0 };
    glm::vec2 cam_start = {0, 0};
    glm::vec2 cam_angles = { 0, 0 };
    glm::vec3 cam_pos = { 0, 0, 0 };
    glm::ivec3 cam_sector = { 0, 0, 0 };
    std::array<bool, 256> keys{false};
    float cam_fov = 90.f;
    bool xrmode = false;
public:
    ~AppBase() = default;
    [[nodiscard]] auto& xr() noexcept { return m_xr; }
    [[nodiscard]] auto& vk() noexcept { return m_vk; }

    static std::vector<shaders::SolidFlatShader::VertexInput> build_triangle() noexcept
    {
        return std::vector<shaders::SolidFlatShader::VertexInput>{
            {.position = {-0.5f, -0.5f, 0.0f, 1.f}, .color = {1, 0, 0, 1}},
            {.position = {-0.5f,  0.5f, 0.0f, 1.f}, .color = {0, 1, 0, 1}},
            {.position = { 0.5f, -0.5f, 0.0f, 1.f}, .color = {0, 0, 1, 1}},
        };
    }
    [[nodiscard]] std::vector<shaders::SolidFlatShader::VertexInput> build_floor(
        const uint32_t cols, const uint32_t rows, const float size) const noexcept
    {
        std::vector<shaders::SolidFlatShader::VertexInput> v;
        const float x_off = -static_cast<float>(rows) / 2 * size;
        const float z_off = -static_cast<float>(cols) / 2 * size;
        for (int i = 0; i < rows; ++i)
        {
            for (int j = 0; j < cols; ++j)
            {
                constexpr float p = 0.05f;
                constexpr float height = 1.5f;
                const float A = static_cast<float>(perlin.octave2D(i * p, j * p, 4) * height);
                const float B = static_cast<float>(perlin.octave2D(i * p, (j+1) * p, 4) * height);
                const float C = static_cast<float>(perlin.octave2D((i+1) * p, (j+1) * p, 4) * height);
                const float D = static_cast<float>(perlin.octave2D((i+1) * p, j * p, 4) * height);
                v.emplace_back(glm::vec4{x_off + i * size, A, z_off +  j * size, 1}, glm::vec4{1, 0, 0, 1});
                v.emplace_back(glm::vec4{x_off + i * size, B, z_off + (j + 1) * size, 1}, glm::vec4{0, 1, 0, 1});
                v.emplace_back(glm::vec4{x_off + (i + 1) * size, D, z_off + j * size, 1}, glm::vec4{1, 1, 0, 1});
                v.emplace_back(glm::vec4{x_off + (i + 1) * size, D, z_off + j * size, 1}, glm::vec4{1, 1, 0, 1});
                v.emplace_back(glm::vec4{x_off + i * size, B, z_off + (j + 1) * size, 1}, glm::vec4{0, 1, 0, 1});
                v.emplace_back(glm::vec4{x_off + (i + 1) * size, C, z_off + (j + 1) * size, 1}, glm::vec4{0, 0, 1, 1});
            }
        }
        return v;
    }
    void init(bool xr_mode) noexcept
    {
        xrmode = xr_mode;

        if (xrmode)
        {
            m_renderpass = m_xr->renderpass();
            m_sample_count = m_xr->sample_count();
        }
        else
        {
            m_renderpass = m_vk->renderpass();
            m_sample_count = m_vk->samples_count();
        }
        solid_flat = std::make_shared<shaders::SolidFlatShader>(m_vk, "Test");
        solid_flat->create(m_renderpass, m_sample_count);

        // Create the grid object
        m_grid = std::make_shared<Grid>();
        m_grid->create(m_vk, 0);

        //generate_chunks();

        vertices = build_floor(5, 5, 0.1f);
        m_vertex_buffer = std::make_shared<vk::Buffer>(m_vk, "VertexBuffer");
        m_vertex_buffer->create(vertices.size() * sizeof(shaders::SolidFlatShader::VertexInput),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        m_vertex_buffer->create_staging(vertices.size() * sizeof(shaders::SolidFlatShader::VertexInput));
        solid_flat->uniform_frame()->create_staging(sizeof(shaders::SolidFlatShader::PerFrameConstants));
        solid_flat->uniform_object()->create_staging(sizeof(shaders::SolidFlatShader::PerObjectBuffer) * shaders::SolidFlatShader::MaxInstance());
        m_vk->exec_immediate("init resources", [this](VkCommandBuffer cmd){
            m_vertex_buffer->update_cmd<shaders::SolidFlatShader::VertexInput>(cmd, vertices);
            if (xrmode)
            {
                m_xr->init_resources(cmd);
            }
            else
            {
                m_vk->init_resources(cmd);
            }
        });
        //solid_flat->uniform()->destroy_staging();
        m_vertex_buffer->destroy_staging();
    }
    [[nodiscard]] std::vector<glm::ivec3> generate_neighbors(const glm::ivec3 origin, int32_t size) const noexcept
    {
        std::vector<glm::ivec3> neighbors;
        const int32_t side = size;
        for (int32_t y = -side; y < side + 1; ++y)
        {
            for (int32_t z = -side; z < side + 1; ++z)
            {
                for (int32_t x = -side; x < side + 1; ++x)
                {
                    const glm::ivec3 sector = glm::ivec3{x, y, z} + origin;
                    neighbors.emplace_back(sector);
                }
            }
        }
        return neighbors;
    }
    void generate_chunks() noexcept
    {
        const uint32_t neighbors_span = 4;
        const auto neighbors = generate_neighbors(cam_sector, neighbors_span);
        const uint32_t chunk_count = pow(neighbors_span * 2 + 1, 3);
        const auto generator = FlatGenerator{32, 2};
        const auto mesher = SimpleMesher<shaders::SolidFlatShader::VertexInput>{};

        std::deque<size_t> chunk_indices;
        for (size_t i = 0; i < m_chunks.size(); ++i)
        {
            if (auto it = std::ranges::find(neighbors, m_chunks[i].sector()); it == neighbors.end())
            {
                chunk_indices.emplace_back(i);
            }
        }

        for (const auto& sector : neighbors)
        {
            // check if it's already present
            if (auto it = std::ranges::find(m_chunks, sector,
                [](auto& p){ return p.sector(); }); it != m_chunks.end())
            {
                continue;
            }
            if (m_chunks.size() < chunk_count)
            {
                auto chunk_data = mesher.mesh(generator.generate(sector));
                const uint32_t set_index = m_chunks.size();
                const glm::vec4 color = glm::gtc::linearRand(glm::vec4(0, 0, 0, 1), glm::vec4(1, 1, 1, 1));
                auto& chunk = m_chunks.emplace_back();
                chunk.create(m_vk, chunk_data, sector, color, set_index + 3);
                if (chunk.index_count() > 0)
                    break;
            }
            else if (chunk_indices.size() > 0)
            {
                LOGI("discard old chunk");
                auto chunk_data = mesher.mesh(generator.generate(sector));
                const uint32_t chunk_index = chunk_indices.back();
                chunk_indices.pop_back();
                const uint32_t set_index = chunk_index;
                const glm::vec4 color = glm::gtc::linearRand(glm::vec4(0, 0, 0, 1), glm::vec4(1, 1, 1, 1));
                auto& chunk = m_chunks[chunk_index];
                chunk = Chunk{};
                chunk.create(m_vk, chunk_data, sector, color, set_index + 3);
                if (chunk.index_count() > 0)
                    break;
            }
        }
    }
    void update_chunks() noexcept
    {
        const glm::vec3 dist = glm::abs(cam_pos - glm::vec3(0.5f) - glm::vec3(cam_sector));
        // LOGI("cam [%f, %f, %f] dist [%f, %f, %f] sector [%d, %d, %d]",
        //     cam_pos.x, cam_pos.y, cam_pos.z, dist.x, dist.y, dist.z, cam_sector.x, cam_sector.y, cam_sector.z);
        if (dist.x > .6 || dist.y > .6 || dist.z > .6)
        {
            cam_sector = glm::floor(cam_pos);
            LOGI("travel to sector [%d, %d, %d]", cam_sector.x, cam_sector.y, cam_sector.z);
        }
        static int countdown = 0;
        if (countdown == 0)
        countdown = (countdown + 1) % 100;
        generate_chunks();
    }
    void render(const vk::utils::FrameContext& frame, const float dt, VkCommandBuffer cmd) const noexcept
    {
        static float time = 0;
        time += dt;

        solid_flat->uniform_frame()->update_cmd(cmd, shaders::SolidFlatShader::PerFrameConstants{
            .ViewProjection = {
                glm::transpose(frame.projection[0] * frame.view[0]),
                glm::transpose(frame.projection[1] * frame.view[1]),
            }
        });

        // const std::array uniforms_object {
        //     shaders::SolidFlatShader::PerObjectBuffer{
        //         .ObjectTransform = glm::transpose(glm::gtc::translate(glm::vec3(0, 0, 1))),
        //     },
        //     shaders::SolidFlatShader::PerObjectBuffer{
        //         .ObjectTransform = glm::transpose(m_xr->hand_pose(0) * glm::gtx::scale(glm::vec3(.1f)))
        //     },
        //     shaders::SolidFlatShader::PerObjectBuffer{
        //         .ObjectTransform = glm::transpose(m_xr->hand_pose(1) * glm::gtx::scale(glm::vec3(.1f)))
        //     },
        // };
        if (!m_chunks.empty())
        {
            std::array<shaders::SolidFlatShader::PerObjectBuffer, shaders::SolidFlatShader::MaxInstance()> uniforms_object{};
            // update grid
            uniforms_object[0].ObjectTransform = glm::identity<glm::mat4>();
            uniforms_object[0].ObjectColor = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
            uniforms_object[0].selected = false;

            for (const Chunk& chunk : m_chunks)
            {
                const size_t i  = chunk.descriptor_set_index();
                uniforms_object[i].ObjectTransform = glm::transpose(chunk.transform());
                uniforms_object[i].ObjectColor = chunk.color();
                uniforms_object[i].selected = (cam_sector == chunk.sector());
            }
            solid_flat->uniform_object()->update_cmd(cmd, std::span(uniforms_object.data(), m_chunks.size() + 3));
        }

        // Renderpass setup
        const std::array rgb{.3f, .3f, .3f};
        const std::array clear_value{
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
        {
            const std::array vertex_buffers{m_grid->vertex_buffer()->buffer()};
            constexpr std::array vertex_buffers_offset{VkDeviceSize{0}};
            vkCmdBindVertexBuffers(cmd, 0, vertex_buffers.size(), vertex_buffers.data(), vertex_buffers_offset.data());
            vkCmdBindIndexBuffer(cmd, m_grid->index_buffer()->buffer(), 0, VK_INDEX_TYPE_UINT32);

            const VkDescriptorSet& descriptor_set = solid_flat->descriptor_set(0);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                solid_flat->layout(), 0, 1, &descriptor_set, 0, nullptr);

            // Draw the cube using its indices
            vkCmdDrawIndexed(cmd, m_grid->index_count(), 1, 0, 0, 0);
        }


        for (const auto& chunk : m_chunks)
        {
            if (chunk.index_count() == 0)
                continue;

            const std::array vertex_buffers{chunk.vertex_buffer()->buffer()};
            constexpr std::array vertex_buffers_offset{VkDeviceSize{0}};
            vkCmdBindVertexBuffers(cmd, 0, vertex_buffers.size(), vertex_buffers.data(), vertex_buffers_offset.data());
            vkCmdBindIndexBuffer(cmd, chunk.index_buffer()->buffer(), 0, VK_INDEX_TYPE_UINT32);

            const VkDescriptorSet& descriptor_set = solid_flat->descriptor_set(chunk.descriptor_set_index());
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                solid_flat->layout(), 0, 1, &descriptor_set, 0, nullptr);

            // Draw the cube using its indices
            vkCmdDrawIndexed(cmd, chunk.index_count(), 1, 0, 0, 0);
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
    }
    glm::mat4 update_flying_camera_angles(const float dt, const GamepadState& gamepad)
    {
        constexpr float dead_zone = 0.05;
        constexpr float steering_speed = 90.f;
        const glm::vec2 rthumb = glm::gtc::make_vec2(gamepad.thumbstick_right);
        const glm::vec2 abs_rthumb = glm::abs(rthumb);
        if (abs_rthumb.x > dead_zone)
        {
            cam_angles.x += glm::radians(rthumb.x * dt * steering_speed);
        }
        if (abs_rthumb.y > dead_zone)
        {
            cam_angles.y += glm::radians(-rthumb.y * dt * steering_speed);
        }
        return glm::gtx::eulerAngleYX(-cam_angles.x, -cam_angles.y);
    }
    void update_flying_camera_pos(const float dt, const GamepadState& gamepad, const glm::mat4& view)
    {
        constexpr float dead_zone = 0.05;
#ifdef _WIN32
        const float speed = keys[VK_SHIFT] ? .25f : 1.f;
#else
        const float speed = 1;
#endif
        if (keys['W'])
        {
            const glm::vec4 forward = glm::vec4{0, 0, -1, 1} * view;
            cam_pos += glm::vec3(forward) * dt * speed;
        }
        if (keys['S'])
        {
            const glm::vec4 forward = glm::vec4{0, 0, 1, 1} * view;
            cam_pos += glm::vec3(forward) * dt * speed;
        }
        if (keys['A'])
        {
            const glm::vec4 forward = glm::vec4{-1, 0, 0, 1} * view;
            cam_pos += glm::vec3(forward) * dt * speed;
        }
        if (keys['D'])
        {
            const glm::vec4 forward = glm::vec4{1, 0, 0, 1} * view;
            cam_pos += glm::vec3(forward) * dt * speed;
        }
        if (keys['Q'])
        {
            cam_pos.y += dt * speed;
        }
        if (keys['E'])
        {
            cam_pos.y -= dt * speed;
        }
        if (fabs(gamepad.thumbstick_left[0]) > dead_zone)
        {
            const glm::vec4 forward = glm::vec4{1, 0, 0, 1} * view;
            cam_pos += glm::vec3(forward) * dt * gamepad.thumbstick_left[0];
        }
        if (fabs(gamepad.thumbstick_left[1]) > dead_zone)
        {
            const glm::vec4 forward = glm::vec4{0, 0, -1, 1} * view;
            cam_pos += glm::vec3(forward) * dt * gamepad.thumbstick_left[1];
        }
    }
    void tick_xrmode(const float dt, const GamepadState& gamepad) noexcept
    {
        m_xr->poll_events();
        if (m_xr->state_active())
        {
            if (m_xr->state_focused() && !m_xr->sync_input())
            {
                LOGE("Failed to sync input");
            }

            m_xr->present([this, dt, &gamepad](const vk::utils::FrameContext& frame){

                auto frame_fixed = frame;
                update_flying_camera_angles(dt, gamepad);
                const auto cam_rot = glm::gtx::eulerAngleX(cam_angles.y) * glm::gtx::eulerAngleY(cam_angles.x);
                const glm::mat4 head_rot = glm::inverse(glm::gtc::mat4_cast(frame.view_quat[0]));
                update_flying_camera_pos(dt, gamepad, head_rot * cam_rot);
                for (uint32_t i = 0; i < 2; i++)
                    frame_fixed.view[i] = frame.view[i];// * cam_rot * glm::gtx::translate(cam_pos);
                render(frame, dt, frame.cmd);
            });

        }
        else
        {
            // Submit empty frames
            m_xr->present([](const vk::utils::FrameContext& frame){});
        }
    }
    void tick_windowed(const float dt, const GamepadState& gamepad) noexcept
    {
        m_vk->present([this, dt, &gamepad](const vk::utils::FrameContext& frame)
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

            auto frame_fixed = frame;
            const auto cam_rot = update_flying_camera_angles(dt, gamepad);
            update_flying_camera_pos(dt, gamepad, frame.view[0] * glm::inverse(cam_rot));
            const float aspect = static_cast<float>(m_size.x) / static_cast<float>(m_size.y);
            frame_fixed.projection[0] = glm::gtc::perspectiveRH_ZO(glm::radians(cam_fov), aspect, 0.01f, 100.f);
            frame_fixed.projection[0][1][1] *= -1.0f; // flip Y for Vulkan
            frame_fixed.view[0] = glm::inverse(glm::gtx::translate(cam_pos) * cam_rot);

            render(frame_fixed, dt, frame.cmd);
        });
    }
    void tick(const float dt, const GamepadState& gamepad) noexcept
    {
        update_chunks();
        if (xrmode)
        {
            tick_xrmode(dt, gamepad);
        }
        else
        {
            tick_windowed(dt, gamepad);
        }
    }
    void on_resize(const uint32_t width, const uint32_t height) noexcept
    {
        LOGI("resize %d %d", width, height);
        m_size = { width, height };
    }
    void on_mouse_wheel(const int32_t x, const int32_t y, const float delta) noexcept
    {
        LOGI("mouse wheel %d %d %f", x, y, delta);
        cam_fov += delta * 10;
    }
    void on_mouse_move(const int32_t x, const int32_t y) noexcept
    {
        // LOGI("mouse moved %d %d", x, y);
        if (dragging)
        {
            const glm::ivec2 pos{x, y};
            const glm::ivec2 delta = pos - drag_start;
            constexpr float multiplier = .25f;
            cam_angles = cam_start + glm::radians(glm::vec2(delta) * multiplier);
        }
    }
    void on_mouse_left_down(const int32_t x, const int32_t y) noexcept
    {
        // LOGI("mouse left down %d %d", x, y);
        dragging = true;
        drag_start = {x, y};
        cam_start = cam_angles;
    }
    void on_mouse_left_up(const int32_t x, const int32_t y) noexcept
    {
        // LOGI("mouse left up %d %d", x, y);
        dragging = false;
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
        keys[keycode] = true;
    }
    void on_key_up(const uint64_t keycode) noexcept
    {
        keys[keycode] = false;
    }
};
}

