module;
#include <cstdint>
#include <memory>
#include <array>
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

export module ce.app.grid;
import ce.vk;
import ce.shaders.solidflat;

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
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST) ||
            !m_vertex_buffer->create_staging(vertices.size() * sizeof(shaders::SolidFlatShader::VertexInput)))
        {
            LOGE("Failed to create cube vertex buffer");
            return false;
        }

        // Create and upload index buffer
        m_index_buffer = std::make_shared<vk::Buffer>(vk, "GridIndexBuffer");
        if (!m_index_buffer->create(indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST) ||
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
}
