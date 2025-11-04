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
#include <mutex>

#include <volk.h>
#include <vk_mem_alloc.h>
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>
#include <tracy/TracyVulkan.hpp>
#include <Jolt/Jolt.h>
#include <ecs/ecs.h>
#include <entityx/entityx.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#else
#define LOGE(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

#ifdef WIN32
#include <windows.h>
#endif

export module ce.app;
import ce.platform;
import ce.platform.globals;
import ce.xr;
import ce.vk;
import ce.vk.buffer;
import ce.vk.texture;
import ce.vk.shader;
import ce.vk.utils;
import ce.shaders.solidflat;
import ce.shaders.solidcolor;
import glm;
import :utils;
import :frustum;
import :chunkgen;
import :chunkmesh;
import :player;
import :audio;
import :server;
import :client;
import :resources;
import :world;
import :systems;
import :globals;
import :shaders;

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

bool operator&(GamepadButton lhs, GamepadButton rhs)
{
    return static_cast<uint8_t>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
}

struct GamepadState
{
    std::array<bool, static_cast<size_t>(GamepadButton::EnumCount)> buttons{false};
    float thumbstick_left[2]{};
    float thumbstick_right[2]{};
    float trigger_left{0};
    float trigger_right{0};
};

struct TransformComponent
{
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale;
};

struct AppStartedEvent : public entityx::Event<AppStartedEvent>
{

};

class AppSystem final : public entityx::System<AppSystem>, public entityx::Receiver<AppSystem>
{
public:
    void update(entityx::EntityManager& entities, entityx::EventManager& events, entityx::TimeDelta dt) override
    {
        //LOGI("AppSystem update");
    }

    void configure(entityx::EntityManager& entities, entityx::EventManager& events) override
    {
        events.subscribe<entityx::EntityCreatedEvent>(*this);
        events.subscribe<entityx::ComponentAddedEvent<TransformComponent>>(*this);
        events.subscribe<AppStartedEvent>(*this);
        LOGI("AppSystem configure");
    }

    void receive(const entityx::EntityCreatedEvent& event) noexcept
    {
        LOGI("AppSystem received: entity create event");
    }

    void receive(const entityx::ComponentAddedEvent<TransformComponent>& event) noexcept
    {
        LOGI("AppSystem received: added TransformComponent event");
    }

    void receive(const AppStartedEvent& event) noexcept
    {
        LOGI("AppSystem received: app started event");
    }
};

class AppBase final
{
    std::shared_ptr<xr::Context> m_xr;
    std::shared_ptr<vk::Context> m_vk;
    shaders::SolidFlatShader::PerFrameConstants uniform{};
    VkRenderPass m_renderpass = VK_NULL_HANDLE;
    VkSampleCountFlagBits m_sample_count = VK_SAMPLE_COUNT_1_BIT;
    glm::ivec2 m_size{0, 0};

    world::World m_world;

    std::array<bool, 256> keys{false};
    uint32_t m_swapchain_count = 0;
    uint64_t m_timeline_value = 0;
    ecs::Manager manager;
    entityx::EntityX ex;

public:
    ~AppBase()
    {
        if (m_vk && m_vk->device())
        {
            vkDeviceWaitIdle(m_vk->device());
        }
        m_world.destroy();
        systems::destroy_systems();
        shaders::destroy_shaders();
        globals::m_resources->garbage_collect(~1ull);
        globals::m_resources->destroy_buffers();
    };
    [[nodiscard]] auto& xr() noexcept { return m_xr; }
    [[nodiscard]] auto& vk() noexcept { return m_vk; }

    void init(const bool xr_mode, const bool server_mode, const bool headless) noexcept
    {
        globals::xrmode = xr_mode;
        globals::server_mode = server_mode;
        globals::headless = headless;

        if (!headless)
        {
            if (globals::xrmode)
            {
                m_renderpass = m_xr->renderpass();
                m_sample_count = m_xr->sample_count();
                m_swapchain_count = m_xr->swapchain_count();
            }
            else
            {
                m_renderpass = m_vk->renderpass();
                m_sample_count = m_vk->samples_count();
                m_swapchain_count = m_vk->swapchain_count();
            }

            shaders::create_shaders({
                .vk = m_vk,
                .renderpass = m_renderpass,
                .swapchain_count = m_swapchain_count,
                .sample_count = m_sample_count
            });

            globals::m_resources = std::make_shared<resources::VulkanResources>();
            globals::m_resources->create_buffers(m_vk);
            globals::m_resources->texture = vk::texture::load_texture(m_vk,
                "assets/grass.png", globals::m_resources->staging_buffer).value();
            globals::m_resources->sampler = vk::texture::create_sampler(m_vk).value();

            m_vk->exec_immediate("init resources", [this](VkCommandBuffer cmd){
                if (globals::xrmode)
                {
                    m_xr->init_resources(cmd);
                }
                else
                {
                    m_vk->init_swapchain(cmd);
                }
            });
        }
        systems::create_systems();
        m_world.create(m_vk);
        ex.systems.add<AppSystem>();
        ex.systems.configure();
        entityx::Entity ent = ex.entities.create();
        ent.assign<TransformComponent>();
        ex.events.emit(AppStartedEvent{});
    }
    void update(const float dt, const vk::utils::FrameContext& frame, const glm::mat4 view) noexcept
    {
        ZoneScoped;
        shaders::reset_descriptors(frame.present_index);
        if (globals::server_mode)
        {
            systems::m_server_system->player_pos = m_world.m_camera.cam_pos;
            systems::m_server_system->player_rot = glm::gtc::quat_cast(view);
            systems::m_server_system->player_vel = glm::gtc::make_vec3(
                m_world.m_player.character->GetLinearVelocity().mF32);
            systems::m_server_system->update(dt, frame, view);
        }
        else
        {
            systems::m_client_system->player_pos = m_world.m_camera.cam_pos;
            systems::m_client_system->player_rot = glm::gtc::quat_cast(glm::inverse(view));
            systems::m_client_system->player_vel = glm::gtc::make_vec3(
                m_world.m_player.character->GetLinearVelocity().mF32);
            systems::m_client_system->update(dt, frame, view);
        }
        m_world.update(dt, frame, view);
        shaders::update_descriptors();
        ex.systems.update_all(dt);
    }
    void render(const vk::utils::FrameContext& frame, const float dt, VkCommandBuffer cmd) noexcept
    {
        ZoneScoped;
        {
            TracyVkZone(m_vk->tracy(), cmd, "Copy Buffers");
            globals::m_resources->exec_copy_buffers(cmd);
        }

        {
            TracyVkZone(m_vk->tracy(), cmd, "Copy Barrier");
            std::vector<VkBufferMemoryBarrier> barriers;
            barriers.reserve(m_world.chunks_manager.m_chunks_state.size());
            for (const auto& [k, state] : m_world.chunks_manager.m_chunks_state)
            {
                barriers.push_back({
                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .buffer = globals::m_resources->args_buffer.buffer(),
                    .offset = state.args_buffer.offset,
                    .size = state.args_buffer.size,
                });
            }

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                0, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data(), 0, nullptr);
        }

        // char zone_name[64];
        // snprintf(zone_name, sizeof(zone_name), "RenderPass %llu", static_cast<uint64_t>(frame.timeline_value));
        // TracyVkZoneTransient(m_vk->tracy(), render_pass_zone, cmd, zone_name, true);
        TracyVkZone(m_vk->tracy(), cmd, "RenderPass");

        // Renderpass setup
        constexpr std::array rgb{.27f, .37f, .5f};
        constexpr std::array clear_value{
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

        if (globals::xrmode)
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

        m_world.render(dt, cmd);

        // End rendering

        constexpr VkSubpassEndInfo subpass_end_info{.sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO};
        vkCmdEndRenderPass2(cmd, &subpass_end_info);

        TracyVkCollect(m_vk->tracy(), cmd);
    }
    glm::mat4 update_flying_camera_angles(const float dt, const GamepadState& gamepad, const xr::TouchControllerState& touch)
    {
        ZoneScoped;
        constexpr float dead_zone = 0.05;
        constexpr float steering_speed = 90.f;
        const glm::vec2 rthumb = glm::gtc::make_vec2(gamepad.thumbstick_right);
        const glm::vec2 abs_rthumb = glm::abs(rthumb);
        if (abs_rthumb.x > dead_zone)
        {
            m_world.m_camera.cam_angles.x += glm::radians(rthumb.x * dt * steering_speed);
        }
        if (abs_rthumb.y > dead_zone)
        {
            m_world.m_camera.cam_angles.y += glm::radians(-rthumb.y * dt * steering_speed);
        }

//        const glm::vec2 touch_abs_rthumb = glm::abs(touch.thumbstick_right);
//        if (touch_abs_rthumb.x > dead_zone)
//        {
//            m_player.cam_angles.x += glm::radians(touch.thumbstick_right.x * dt * steering_speed);
//        }
//        if (touch_abs_rthumb.y > dead_zone)
//        {
//            m_player.cam_angles.y -= glm::radians(-touch.thumbstick_right.y * dt * steering_speed);
//        }
        return glm::gtx::eulerAngleYX(-m_world.m_camera.cam_angles.x, -m_world.m_camera.cam_angles.y);
    }
    void update_flying_camera_pos(const float dt, const GamepadState& gamepad,
        const xr::TouchControllerState& touch, const glm::mat4& view)
    {
        ZoneScoped;
        constexpr float dead_zone = 0.05;
#ifdef _WIN32
        float speed = 2.f;
        if (keys[VK_SHIFT])
            speed = speed * 0.25f;
        if (keys[VK_CONTROL])
            speed = speed * 2.f;
        const bool action_build_new = keys[VK_SPACE] ||
            gamepad.buttons[std::to_underlying(GamepadButton::ShoulderLeft)];
        const bool action_break_new = keys['B'] ||
            gamepad.buttons[std::to_underlying(GamepadButton::ShoulderRight)];
        const bool action_respawn_new = keys['F'] ||
            gamepad.buttons[std::to_underlying(GamepadButton::Menu)];
        const bool action_frustum_new = keys['C'];
        const bool action_physics_record_new = keys['R'];
        speed = speed + 3.f * static_cast<float>(gamepad.buttons[std::to_underlying(GamepadButton::ThumbLeft)]);
#else
        const float speed = 3.f;
        const bool action_build_new = touch.trigger_right > 0.5f;
        const bool action_break_new = touch.trigger_left > 0.5f;
        const bool action_respawn_new = touch.buttons[std::to_underlying(GamepadButton::Menu)];
        const bool action_frustum_new = touch.buttons[std::to_underlying(GamepadButton::X)];
        const bool action_physics_record_new = false;
#endif
        static bool action_physics_record = false;
        if (action_physics_record != action_physics_record_new)
        {
            action_physics_record = action_physics_record_new;
            if (action_physics_record)
            {
                LOGI("Jolt recording started");
                systems::m_physics_system->start_recording();
                //LOGE("Jolt recording disabled - enable to use the feature");
            }
        }
        static bool action_frustum = false;
        if (action_frustum != action_frustum_new)
        {
            action_frustum = action_frustum_new;
            if (action_frustum)
            {
                // update_frustum = !update_frustum;
            }
        }
        static bool action_respawn = false;
        if (action_respawn != action_respawn_new)
        {
            action_respawn = action_respawn_new;
            if (action_respawn)
            {
                const auto p = m_world.m_player.character->GetPosition();
                m_world.m_player.character->SetPosition({p.GetX(), 20, p.GetZ()});
                m_world.m_player.character->SetLinearVelocity(JPH::Vec3::sZero());
            }
        }
        static bool action_build = false;
        if (action_build_new != action_build)
        {
            action_build = action_build_new;
            if (action_build)
            {
                const glm::vec4 forward = glm::vec4{0, 0, -1, 1} * view;
                m_world.chunks_manager.build_block(m_world.m_camera.cam_pos, forward);
            }
        }
        static bool action_break = false;
        if (action_break_new != action_break)
        {
            action_break = action_break_new;
            if (action_break)
            {
                const glm::vec4 forward = glm::vec4{0, 0, -1, 1} * view;
                m_world.chunks_manager.break_block(m_world.m_camera.cam_pos, forward);
            }
        }
#ifdef _WIN32
        const float fx = (fabs(gamepad.thumbstick_left[0]) > dead_zone ? gamepad.thumbstick_left[0] : 0.f) +
            (keys['D'] ? 1.f : 0.f) + (keys['A'] ? -1.f : 0.f);
        const float fz = (fabs(gamepad.thumbstick_left[1]) > dead_zone ? gamepad.thumbstick_left[1] : 0.f) +
            (keys['W'] ? 1.f : 0.f) + (keys['S'] ? -1.f : 0.f);
        const bool action_jump_new = keys['E'] ||
            gamepad.buttons[std::to_underlying(xr::TouchControllerButton::A)];
#else
        const float fx = fabs(touch.thumbstick_left[0]) > dead_zone ? touch.thumbstick_left[0] : 0.f;
        const float fz = fabs(touch.thumbstick_left[1]) > dead_zone ? touch.thumbstick_left[1] : 0.f;
        bool action_jump_new = touch.buttons[std::to_underlying(xr::TouchControllerButton::A)];
#endif
        float fy = 0.f;
        static bool action_jump = false;
        if (action_jump_new != action_jump)
        {
            action_jump = action_jump_new;
            if (action_jump && m_world.m_player.character->IsSupported())
                fy += 5.f;
        }
        const glm::vec3 forward = glm::vec4{fx, 0, -fz, 1} * view;
        if (glm::abs(glm::length(forward)) > 0.f || fy > 0.f)
        {
            const auto step = glm::vec3(forward) * speed;
            const auto current_velocity = glm::gtc::make_vec3(m_world.m_player.character->GetLinearVelocity().mF32);
            const auto velocity = glm::vec3(step.x, current_velocity.y, step.z);
            const auto v = glm::gtc::lerp(current_velocity, velocity, dt * 3.f);
            m_world.m_player.character->SetLinearVelocity(JPH::Vec3(v.x, fy + v.y, v.z));
        }
        else
        {
            const auto current_velocity = glm::gtc::make_vec3(m_world.m_player.character->GetLinearVelocity().mF32);
            const auto v = glm::gtc::lerp(current_velocity, glm::vec3(0, current_velocity.y, 0), dt * 10.f);
            m_world.m_player.character->SetLinearVelocity(JPH::Vec3(v.x, v.y, v.z));
        }
    }
    void tick_xrmode(const float dt, const GamepadState& gamepad) noexcept
    {
        ZoneScoped;
        m_xr->poll_events();
        if (m_xr->state_active())
        {
            if (m_xr->state_focused() && !m_xr->sync_input())
            {
                LOGE("Failed to sync input");
            }

            vk::utils::FrameContext frame_fixed;
            m_xr->present(
            [this, dt, &gamepad, &frame_fixed](const vk::utils::FrameContext& frame)
            {
                frame_fixed = frame;
                update_flying_camera_angles(dt, gamepad, m_xr->touch_controllers());
                const auto cam_rot = glm::gtx::eulerAngleX(m_world.m_camera.cam_angles.y) * glm::gtx::eulerAngleY(m_world.m_camera.cam_angles.x);
                const glm::mat4 head_rot = glm::inverse(glm::gtc::mat4_cast(frame.view_quat[0]));
                update_flying_camera_pos(dt, gamepad, m_xr->touch_controllers(), head_rot * cam_rot);
                for (uint32_t i = 0; i < 2; i++)
                    frame_fixed.view[i] = frame.view[i] * glm::inverse(cam_rot * glm::gtx::translate(m_world.m_camera.cam_pos));
                update(dt, frame_fixed, head_rot * cam_rot);
            },
            [this, dt, &frame_fixed](const vk::utils::FrameContext& frame){
                frame_fixed.cmd = frame.cmd;
                render(frame_fixed, dt, frame.cmd);
            });
        }
        else
        {
            // Submit empty frames
            const auto nop = [](const vk::utils::FrameContext&){};
            m_xr->present(nop, nop);
        }
    }
    void tick_windowed(const float dt, const GamepadState& gamepad) noexcept
    {
        ZoneScoped;
        vk::utils::FrameContext frame_fixed;
        m_vk->present(
        [this, dt, &gamepad, &frame_fixed](const vk::utils::FrameContext& frame)
        {
            frame_fixed = frame;
            const auto cam_rot = update_flying_camera_angles(dt, gamepad, {});
            update_flying_camera_pos(dt, gamepad, {}, frame.view[0] * glm::inverse(cam_rot));
            const float aspect = static_cast<float>(m_size.x) / static_cast<float>(m_size.y);
            frame_fixed.projection[0] = glm::gtc::perspectiveRH_ZO(glm::radians(m_world.m_camera.cam_fov), aspect, 0.01f, 100.f);
            frame_fixed.projection[0][1][1] *= -1.0f; // flip Y for Vulkan
            frame_fixed.view[0] = glm::inverse(glm::gtx::translate(m_world.m_camera.cam_pos) * cam_rot);

            update(dt, frame_fixed, frame.view[0] * glm::inverse(cam_rot));
        },
        [this, dt, &frame_fixed](const vk::utils::FrameContext& frame)
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
            frame_fixed.cmd = frame.cmd;
            render(frame_fixed, dt, frame.cmd);
        });
    }
    void tick(const float dt, const GamepadState& gamepad) noexcept
    {
        ZoneScoped;
        if (globals::server_mode)
        {
            systems::m_physics_system->tick(dt);
            systems::m_server_system->tick(dt);
        }
        else
        {
            systems::m_client_system->tick(dt);
            systems::m_physics_system->tick(dt);
        }

        if (!globals::headless)
        {
            m_world.tick(dt);
            if (globals::xrmode)
            {
                m_timeline_value = m_xr->timeline_value().value_or(0);
                globals::m_resources->garbage_collect(m_timeline_value);
                tick_xrmode(dt, gamepad);
            }
            else
            {
                m_timeline_value = m_vk->timeline_value().value_or(0);
                globals::m_resources->garbage_collect(m_timeline_value);
                tick_windowed(dt, gamepad);
            }
        }
    }
    void on_resize(const uint32_t width, const uint32_t height) noexcept
    {
        LOGI("resize %d %d", width, height);
        m_size = { width, height };
        // m_vk->resize_swapchain();
    }
    void on_mouse_wheel(const int32_t x, const int32_t y, const float delta) noexcept
    {
        LOGI("mouse wheel %d %d %f", x, y, delta);
        m_world.m_camera.cam_fov += delta * 10;
    }
    void on_mouse_move(const int32_t x, const int32_t y) noexcept
    {
        // LOGI("mouse moved %d %d", x, y);
        if (m_world.m_camera.dragging)
        {
            const glm::ivec2 pos{x, y};
            const glm::ivec2 delta = pos - m_world.m_camera.drag_start;
            constexpr float multiplier = .25f;
            m_world.m_camera.cam_angles = m_world.m_camera.cam_start + glm::radians(glm::vec2(delta) * multiplier);
        }
    }
    void on_mouse_left_down(const int32_t x, const int32_t y) noexcept
    {
        // LOGI("mouse left down %d %d", x, y);
        m_world.m_camera.dragging = true;
        m_world.m_camera.drag_start = {x, y};
        m_world.m_camera.cam_start = m_world.m_camera.cam_angles;
    }
    void on_mouse_left_up(const int32_t x, const int32_t y) noexcept
    {
        // LOGI("mouse left up %d %d", x, y);
        m_world.m_camera.dragging = false;
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
            //exit(0);
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
