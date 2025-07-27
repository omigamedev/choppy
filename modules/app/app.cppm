module;
#include <array>
#include <span>
#include <vector>
#include <memory>
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
import ce.shaders.solidflat;
import glm;

export namespace ce::app
{
class Cube
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
class AppBase final
{
    std::shared_ptr<xr::Context> m_xr;
    std::shared_ptr<vk::Context> m_vk;
    std::shared_ptr<shaders::SolidFlatShader> solid_flat;
    std::shared_ptr<vk::Buffer> m_vertex_buffer;
    std::vector<shaders::SolidFlatShader::VertexInput> vertices;
    shaders::SolidFlatShader::PerFrameConstants uniform{};
	siv::PerlinNoise perlin{ std::random_device{} };
    std::shared_ptr<Cube> m_cube;
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
                constexpr float p = 0;//0.05f;
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
    void init() noexcept
    {
        m_xr->bind_input();
        solid_flat = std::make_shared<shaders::SolidFlatShader>(m_vk, "Test");
        solid_flat->create(m_xr->renderpass());

        // Create the cube object
        m_cube = std::make_shared<Cube>();
        m_cube->create(m_vk); // This handles creating and uploading the cube's data

        vertices = build_floor(50, 50, 0.1f);
        m_vertex_buffer = std::make_shared<vk::Buffer>(m_vk, "VertexBuffer");
        m_vertex_buffer->create(vertices.size() * sizeof(shaders::SolidFlatShader::VertexInput),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        m_vertex_buffer->create_staging(vertices.size() * sizeof(shaders::SolidFlatShader::VertexInput));
        solid_flat->uniform_frame()->create_staging(sizeof(shaders::SolidFlatShader::PerFrameConstants));
        solid_flat->uniform_object()->create_staging(sizeof(shaders::SolidFlatShader::PerObjectBuffer) * 3);
        m_vk->exec_immediate("init resources", [this](VkCommandBuffer cmd){
            m_vertex_buffer->update_cmd<shaders::SolidFlatShader::VertexInput>(cmd, vertices);
            for (VkImage img : m_xr->swapchain_depth_images())
            {
                constexpr VkImageSubresourceRange subresource_range = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 2};
                const VkImageMemoryBarrier barrier{
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .srcAccessMask = 0,
                    .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                        | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    .image = img,
                    .subresourceRange = subresource_range
                };
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);
            }
        });
        //solid_flat->uniform()->destroy_staging();
        m_vertex_buffer->destroy_staging();
    }
    void render(const xr::FrameContext& frame, const float dt, VkCommandBuffer cmd) const noexcept
    {
        static float time = 0;
        time += dt;

        solid_flat->uniform_frame()->update_cmd(cmd, shaders::SolidFlatShader::PerFrameConstants{
            .ViewProjection = {
                glm::transpose(frame.projection[0] * frame.view[0]),
                glm::transpose(frame.projection[1] * frame.view[1]),
            }
        });

        const std::array uniforms_object {
            shaders::SolidFlatShader::PerObjectBuffer{
                .ObjectTransform = glm::identity<glm::mat4>(),
            },
            shaders::SolidFlatShader::PerObjectBuffer{
                .ObjectTransform = glm::transpose(m_xr->hand_pose(0) * glm::gtx::scale(glm::vec3(.1f)))
            },
            shaders::SolidFlatShader::PerObjectBuffer{
                .ObjectTransform = glm::transpose(m_xr->hand_pose(1) * glm::gtx::scale(glm::vec3(.1f)))
            },
        };
        solid_flat->uniform_object()->update_cmd(cmd, uniforms_object);

        const std::array rgb{.3f, .3f, .3f};
        const std::array clear_value{
            VkClearValue{.color = {rgb[0], rgb[1], rgb[2], 1.f}},
            VkClearValue{.depthStencil = {1.f, 0u}}
        };
        const std::array renderpass_views{frame.color_view, frame.depth_view};
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

        const VkViewport viewport = m_xr->viewport();
        vkCmdSetViewportWithCount(cmd, 1, &viewport);
        const VkRect2D scissor = m_xr->scissor();
        vkCmdSetScissorWithCount(cmd, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, solid_flat->pipeline());

        {
            const std::array vertex_buffers{m_vertex_buffer->buffer()};
            constexpr std::array vertex_buffers_offset{VkDeviceSize{0}};
            vkCmdBindVertexBuffers(cmd, 0, vertex_buffers.size(), vertex_buffers.data(), vertex_buffers_offset.data());
            const VkDescriptorSet& descriptor_set = solid_flat->descriptor_set(0);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                solid_flat->layout(), 0, 1, &descriptor_set, 0, nullptr);
            vkCmdDraw(cmd, vertices.size(), 1, 0, 0);
        }

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

        // End rendering

        constexpr VkSubpassEndInfo subpass_end_info{.sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO};
        vkCmdEndRenderPass2(cmd, &subpass_end_info);
    }
    void tick(const float dt) noexcept
    {
        m_xr->poll_events();
        if (m_xr->state_active())
        {
            if (m_xr->state_focused() && !m_xr->sync_input())
            {
                LOGE("Failed to sync input");
            }
            m_xr->present([this, dt](const xr::FrameContext& frame){
                m_xr->sync_pose(frame.display_time);
                m_vk->exec("render_eye", [this, frame, dt](VkCommandBuffer cmd){
                    render(frame, dt, cmd);
                });
            });
        }
        else
        {
            // Submit empty frames
            m_xr->present([](const xr::FrameContext& frame){});
        }
    }
};
}

