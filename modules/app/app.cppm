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
import ce.app.grid;
import ce.app.cube;
import ce.app.utils;
import ce.app.chunkgen;
import ce.app.chunkmesh;
import glm;

export namespace ce::app
{
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
struct PlayerState
{
    bool dragging = false;
    glm::ivec2 drag_start = { 0, 0 };
    float cam_fov = 90.f;
    glm::vec2 cam_start = {0, 0};
    glm::vec2 cam_angles = { 0, 0 };
    glm::vec3 cam_pos = { 0, 0, 0 };
    glm::ivec3 cam_sector = { 0, 0, 0 };
    std::array<bool, 256> keys{false};
};
class AppBase final
{
    std::shared_ptr<xr::Context> m_xr;
    std::shared_ptr<vk::Context> m_vk;
    std::shared_ptr<shaders::SolidFlatShader> solid_flat;
    shaders::SolidFlatShader::PerFrameConstants uniform{};
	siv::PerlinNoise perlin{ std::random_device{} };
    std::shared_ptr<Grid> m_grid;
    std::vector<Chunk> m_chunks;
    VkRenderPass m_renderpass = VK_NULL_HANDLE;
    VkSampleCountFlagBits m_sample_count = VK_SAMPLE_COUNT_1_BIT;
    glm::ivec2 m_size{0, 0};
    std::vector<std::pair<VkFence, std::shared_ptr<vk::Buffer>>> delete_buffers;
    std::shared_ptr<vk::Buffer> m_vertex_buffer;
    PlayerState m_player{};
    bool xrmode = false;
public:
    ~AppBase()
    {
        // Wait until all commands are done
        if (m_vk && m_vk->device())
        {
            vkDeviceWaitIdle(m_vk->device());
        }
        for (auto& c : m_chunks)
        {
            m_vertex_buffer->subfree(c.buffer());
        }
    };
    [[nodiscard]] auto& xr() noexcept { return m_xr; }
    [[nodiscard]] auto& vk() noexcept { return m_vk; }

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

        // Create and upload vertex buffer
        m_vertex_buffer = std::make_shared<vk::Buffer>(m_vk, "CubeVertexBuffer");
        if (!m_vertex_buffer->create(512 * (1 << 20),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
            !m_vertex_buffer->create_staging(512 * (1 << 20)))
        {
            LOGE("Failed to create cube vertex buffer");
        }

        solid_flat->uniform_frame()->create_staging(sizeof(shaders::SolidFlatShader::PerFrameConstants));
        solid_flat->uniform_object()->create_staging(sizeof(shaders::SolidFlatShader::PerObjectBuffer) * shaders::SolidFlatShader::MaxInstance());
        m_vk->exec_immediate("init resources", [this](VkCommandBuffer cmd){
            if (xrmode)
            {
                m_xr->init_resources(cmd);
            }
            else
            {
                m_vk->init_resources(cmd);
            }
        });
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
    void generate_chunks(const vk::utils::FrameContext& frame) noexcept
    {
        const glm::vec3 dist = glm::abs(m_player.cam_pos - glm::vec3(0.5f) - glm::vec3(m_player.cam_sector));
        if (dist.x > .6 || dist.y > .6 || dist.z > .6)
        {
            m_player.cam_sector = glm::floor(m_player.cam_pos);
            LOGI("travel to sector [%d, %d, %d]", m_player.cam_sector.x, m_player.cam_sector.y, m_player.cam_sector.z);
        }

        constexpr uint32_t neighbors_span = 7;
        constexpr uint32_t chunk_count = pow(neighbors_span * 2 + 1, 3);
        const auto neighbors = generate_neighbors(m_player.cam_sector, neighbors_span);
        const auto generator = FlatGenerator{8, 2};
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
                const auto chunk_data = mesher.mesh(generator.generate(sector));
                const uint32_t set_index = m_chunks.size();
                const glm::vec4 color = glm::gtc::linearRand(glm::vec4(0, 0, 0, 1), glm::vec4(1, 1, 1, 1));
                auto& chunk = m_chunks.emplace_back();
                if (!chunk_data.vertices.empty())
                {
                    size_t size = chunk_data.vertices.size() * sizeof(shaders::SolidFlatShader::VertexInput);
                    const auto buffer = m_vertex_buffer->suballoc(size);
                    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(chunk_data.vertices.data());
                    m_vertex_buffer->update_cmd(frame.cmd, std::span{ptr, size}, buffer.value().offset, buffer.value().offset);
                    chunk.create(sector, color, set_index + 3, chunk_data.vertices.size(), buffer.value());
                    LOGI("Generate NEW chunk");
                    break;
                }
                chunk.create(sector, color, set_index + 3, 0, {});
            }
            else if (!chunk_indices.empty())
            {
                auto chunk_data = mesher.mesh(generator.generate(sector));
                const uint32_t chunk_index = chunk_indices.back();
                chunk_indices.pop_back();
                const uint32_t set_index = chunk_index;
                const glm::vec4 color = glm::gtc::linearRand(glm::vec4(0, 0, 0, 1), glm::vec4(1, 1, 1, 1));
                auto& chunk = m_chunks[chunk_index];
                m_vertex_buffer->subfree(chunk.buffer());
                chunk = Chunk{};
                if (!chunk_data.vertices.empty())
                {
                    size_t size = chunk_data.vertices.size() * sizeof(shaders::SolidFlatShader::VertexInput);
                    const auto buffer = m_vertex_buffer->suballoc(size);
                    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(chunk_data.vertices.data());
                    m_vertex_buffer->update_cmd(frame.cmd, std::span{ptr, size}, buffer.value().offset, buffer.value().offset);
                    chunk.create(sector, color, set_index + 3, chunk_data.vertices.size(), buffer.value());
                    LOGI("Generate REUSE chunk");
                    break;
                }
                chunk.create(sector, color, set_index + 3, 0, {});
            }
        }
    }
    void render(const vk::utils::FrameContext& frame, const float dt, VkCommandBuffer cmd) const noexcept
    {
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
            static std::array<shaders::SolidFlatShader::PerObjectBuffer, shaders::SolidFlatShader::MaxInstance()> uniforms_object{};
            // update grid
            uniforms_object[0].ObjectTransform = glm::identity<glm::mat4>();
            uniforms_object[0].ObjectColor = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
            uniforms_object[0].selected = false;

            for (const Chunk& chunk : m_chunks)
            {
                const size_t i  = chunk.descriptor_set_index();
                uniforms_object[i].ObjectTransform = glm::transpose(chunk.transform());
                uniforms_object[i].ObjectColor = chunk.color();
                uniforms_object[i].selected = (m_player.cam_sector == chunk.sector());
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
            if (chunk.vertex_count() == 0)
                continue;

            const std::array vertex_buffers{m_vertex_buffer->buffer()};
            const std::array vertex_buffers_offset{chunk.buffer_offset()};
            vkCmdBindVertexBuffers(cmd, 0, vertex_buffers.size(), vertex_buffers.data(), vertex_buffers_offset.data());

            const VkDescriptorSet& descriptor_set = solid_flat->descriptor_set(chunk.descriptor_set_index());
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                solid_flat->layout(), 0, 1, &descriptor_set, 0, nullptr);

            // Draw the cube using its indices
            // vkCmdDrawIndexed(cmd, chunk.index_count(), 1, 0, 0, 0);
            vkCmdDraw(cmd, chunk.vertex_count(), 1, 0, 0);
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
    glm::mat4 update_flying_camera_angles(const float dt, const GamepadState& gamepad, const xr::TouchControllerState& touch)
    {
        constexpr float dead_zone = 0.05;
        constexpr float steering_speed = 90.f;
        const glm::vec2 rthumb = glm::gtc::make_vec2(gamepad.thumbstick_right);
        const glm::vec2 abs_rthumb = glm::abs(rthumb);
        if (abs_rthumb.x > dead_zone)
        {
            m_player.cam_angles.x += glm::radians(rthumb.x * dt * steering_speed);
        }
        if (abs_rthumb.y > dead_zone)
        {
            m_player.cam_angles.y += glm::radians(-rthumb.y * dt * steering_speed);
        }

        const glm::vec2 touch_abs_rthumb = glm::abs(touch.thumbstick_right);
        if (touch_abs_rthumb.x > dead_zone)
        {
            m_player.cam_angles.x += glm::radians(touch.thumbstick_right.x * dt * steering_speed);
        }
        if (touch_abs_rthumb.y > dead_zone)
        {
            m_player.cam_angles.y -= glm::radians(-touch.thumbstick_right.y * dt * steering_speed);
        }
        return glm::gtx::eulerAngleYX(-m_player.cam_angles.x, -m_player.cam_angles.y);
    }
    void update_flying_camera_pos(const float dt, const GamepadState& gamepad,
        const xr::TouchControllerState& touch, const glm::mat4& view)
    {
        constexpr float dead_zone = 0.05;
#ifdef _WIN32
        const float speed = m_player.keys[VK_SHIFT] ? .25f : 1.f;
#else
        const float speed = 1;
#endif
        if (m_player.keys['W'])
        {
            const glm::vec4 forward = glm::vec4{0, 0, -1, 1} * view;
            m_player.cam_pos += glm::vec3(forward) * dt * speed;
        }
        if (m_player.keys['S'])
        {
            const glm::vec4 forward = glm::vec4{0, 0, 1, 1} * view;
            m_player.cam_pos += glm::vec3(forward) * dt * speed;
        }
        if (m_player.keys['A'])
        {
            const glm::vec4 forward = glm::vec4{-1, 0, 0, 1} * view;
            m_player.cam_pos += glm::vec3(forward) * dt * speed;
        }
        if (m_player.keys['D'])
        {
            const glm::vec4 forward = glm::vec4{1, 0, 0, 1} * view;
            m_player.cam_pos += glm::vec3(forward) * dt * speed;
        }
        if (m_player.keys['Q'])
        {
            m_player.cam_pos.y -= dt * speed;
        }
        if (m_player.keys['E'])
        {
            m_player.cam_pos.y += dt * speed;
        }
        if (fabs(gamepad.thumbstick_left[0]) > dead_zone)
        {
            const glm::vec4 forward = glm::vec4{1, 0, 0, 1} * view;
            m_player.cam_pos += glm::vec3(forward) * dt * gamepad.thumbstick_left[0];
        }
        if (fabs(gamepad.thumbstick_left[1]) > dead_zone)
        {
            const glm::vec4 forward = glm::vec4{0, 0, -1, 1} * view;
            m_player.cam_pos += glm::vec3(forward) * dt * gamepad.thumbstick_left[1];
        }
        if (fabs(touch.thumbstick_left.x) > dead_zone)
        {
            const glm::vec4 forward = glm::vec4{1, 0, 0, 1} * view;
            m_player.cam_pos += glm::vec3(forward) * dt * touch.thumbstick_left.x;
        }
        if (fabs(touch.thumbstick_left.y) > dead_zone)
        {
            const glm::vec4 forward = glm::vec4{0, 0, -1, 1} * view;
            m_player.cam_pos += glm::vec3(forward) * dt * touch.thumbstick_left.y;
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
                generate_chunks(frame);
                auto frame_fixed = frame;
                update_flying_camera_angles(dt, gamepad, m_xr->touch_controllers());
                const auto cam_rot = glm::gtx::eulerAngleX(m_player.cam_angles.y) * glm::gtx::eulerAngleY(m_player.cam_angles.x);
                const glm::mat4 head_rot = glm::inverse(glm::gtc::mat4_cast(frame.view_quat[0]));
                update_flying_camera_pos(dt, gamepad, m_xr->touch_controllers(), head_rot * cam_rot);
                for (uint32_t i = 0; i < 2; i++)
                    frame_fixed.view[i] = frame.view[i] * glm::inverse(cam_rot * glm::gtx::translate(m_player.cam_pos));
                render(frame_fixed, dt, frame.cmd);
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
            generate_chunks(frame);
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
            const auto cam_rot = update_flying_camera_angles(dt, gamepad, {});
            update_flying_camera_pos(dt, gamepad, {}, frame.view[0] * glm::inverse(cam_rot));
            const float aspect = static_cast<float>(m_size.x) / static_cast<float>(m_size.y);
            frame_fixed.projection[0] = glm::gtc::perspectiveRH_ZO(glm::radians(m_player.cam_fov), aspect, 0.01f, 100.f);
            frame_fixed.projection[0][1][1] *= -1.0f; // flip Y for Vulkan
            frame_fixed.view[0] = glm::inverse(glm::gtx::translate(m_player.cam_pos) * cam_rot);

            render(frame_fixed, dt, frame.cmd);
        });
    }
    void tick(const float dt, const GamepadState& gamepad) noexcept
    {
        garbage_collect();
        if (xrmode)
        {
            tick_xrmode(dt, gamepad);
        }
        else
        {
            tick_windowed(dt, gamepad);
        }
    }
    void garbage_collect() noexcept
    {
        for (auto& [fence, buffer] : delete_buffers)
        {
            vkWaitForFences(m_vk->device(), 1, &fence, VK_TRUE, UINT64_MAX);
        }
        delete_buffers.clear();
    }
    void on_resize(const uint32_t width, const uint32_t height) noexcept
    {
        LOGI("resize %d %d", width, height);
        m_size = { width, height };
    }
    void on_mouse_wheel(const int32_t x, const int32_t y, const float delta) noexcept
    {
        LOGI("mouse wheel %d %d %f", x, y, delta);
        m_player.cam_fov += delta * 10;
    }
    void on_mouse_move(const int32_t x, const int32_t y) noexcept
    {
        // LOGI("mouse moved %d %d", x, y);
        if (m_player.dragging)
        {
            const glm::ivec2 pos{x, y};
            const glm::ivec2 delta = pos - m_player.drag_start;
            constexpr float multiplier = .25f;
            m_player.cam_angles = m_player.cam_start + glm::radians(glm::vec2(delta) * multiplier);
        }
    }
    void on_mouse_left_down(const int32_t x, const int32_t y) noexcept
    {
        // LOGI("mouse left down %d %d", x, y);
        m_player.dragging = true;
        m_player.drag_start = {x, y};
        m_player.cam_start = m_player.cam_angles;
    }
    void on_mouse_left_up(const int32_t x, const int32_t y) noexcept
    {
        // LOGI("mouse left up %d %d", x, y);
        m_player.dragging = false;
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
        m_player.keys[keycode] = true;
    }
    void on_key_up(const uint64_t keycode) noexcept
    {
        m_player.keys[keycode] = false;
    }
};
}

