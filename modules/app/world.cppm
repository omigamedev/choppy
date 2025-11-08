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
#include <map>
#include <mutex>

#include <enet.h>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>
#include <tracy/TracyVulkan.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/Character.h>

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

export module ce.app:world;
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
import :chunksman;
import :systems;
import :globals;
import :physics;
import :shaders;

export namespace ce::app::world
{
struct World
{
    std::shared_ptr<vk::Context> m_vk;
    player::PlayerState m_player{};
    player::PlayerCamera m_camera{};
    resources::Geometry m_cube;
    std::vector<resources::Geometry> m_obj_meshes;
    vk::texture::Texture m_obj_texture;

    vk::BufferSuballocation shader_flat_frame{};
    vk::BufferSuballocation shader_color_frame{};
    vk::BufferSuballocation shader_textured_frame{};
    VkDescriptorSet shader_flat_frame_set = VK_NULL_HANDLE;
    VkDescriptorSet shader_color_frame_set = VK_NULL_HANDLE;
    VkDescriptorSet shader_textured_frame_set = VK_NULL_HANDLE;
    VkDescriptorSet shader_textured_material_set = VK_NULL_HANDLE;

    chunksman::ChunksManager chunks_manager;

    bool update_frustum = true;

    bool create(const std::shared_ptr<vk::Context>& vulkan_context) noexcept
    {
        if (systems::m_physics_system)
            m_player.character = systems::m_physics_system->create_character();

        if (vulkan_context)
        {
            m_vk = vulkan_context;
            m_cube = globals::m_resources->create_cube<shaders::SolidColorShader>();

            m_vk->exec_immediate("init world resources", [this](VkCommandBuffer cmd){
                globals::m_resources->exec_copy_buffers(cmd);
            });
        }

        if (systems::m_server_system)
        {
            systems::m_server_system->on_block_action = [this](const messages::BlockActionMessage& block)
            {
                systems::m_server_system->broadcast_message(ENET_PACKET_FLAG_RELIABLE, block);
                if (block.action == messages::BlockActionMessage::ActionType::Break)
                {
                    chunks_manager.break_block(block.world_cell);
                }
                else
                {
                    chunks_manager.build_block(block.world_cell);
                }
            };
            systems::m_server_system->on_chunk_data_request = [this](ENetPeer* peer, const messages::ChunkDataMessage& chunk)
            {
                systems::m_server_system->send_message(peer, ENET_PACKET_FLAG_RELIABLE, messages::ChunkDataMessage{
                    .message_direction = messages::MessageDirection::Response,
                    .sector = chunk.sector,
                    .data = chunks_manager.generator.serialize(chunk.sector),
                });
            };
        }
        if (systems::m_client_system)
        {
            systems::m_client_system->on_block_action = [this](const messages::BlockActionMessage& block)
            {
                if (block.action == messages::BlockActionMessage::ActionType::Break)
                {
                    chunks_manager.break_block(block.world_cell);
                }
                else
                {
                    chunks_manager.build_block(block.world_cell);
                }
            };
            systems::m_client_system->on_chunk_data = [this](const messages::ChunkDataMessage& chunk)
            {
                if (!chunk.data.empty())
                {
                    // chunks_manager.generator.deserialize_apply(chunk.sector, chunk.data);
                    chunks_manager.chunks_to_sync.emplace_back(chunk.sector, chunk.data);
                }
            };
        }
        chunks_manager.create();

        if (!globals::server_mode)
        {
            if (const auto result = globals::m_resources->load_obj<shaders::TexturedShader::VertexInput>(
                "assets/models/broccoli_v3.1_Cycles.obj"))
            {
                m_obj_meshes = result.value();
                if (const auto tex_result = vk::texture::load_texture(m_vk,
                    "assets/models/broccoli_brobody_Mat_BaseColor.png", globals::m_resources->staging_buffer))
                {
                    m_obj_texture = tex_result.value();
                }
            }
            else
            {
                LOGE("failed to load cube.obj");
            }
        }
        return true;
    }
    void destroy() noexcept
    {
        if (m_vk)
        {
            globals::m_resources->destroy_geometry(m_cube, 0);
            for (auto& geo : m_obj_meshes)
            {
                globals::m_resources->destroy_geometry(geo, 0);
            }
            m_obj_meshes.clear();
        }
        m_player.destroy();;
        chunks_manager.destroy();
    }
    void update(const float dt, const vk::utils::FrameContext& frame, glm::mat4 view) noexcept
    {
        chunks_manager.cam_pos = m_camera.cam_pos;
        //chunks_manager.cam_sector = m_camera.cam_sector;
        chunks_manager.update_chunks(frame);

        if (const auto sb = globals::m_resources->staging_buffer.suballoc(
            sizeof(shaders::SolidFlatShader::PerFrameConstants), 64))
        {
            const auto dst_sb = globals::m_resources->frame_buffer.suballoc(sb->size, 64);

            auto tint = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
            auto fogColor = glm::vec4(.27f, .37f, .5f, 1.f);
            auto fogStart = 20.f;
            auto fogEnd = 50.f;
            const glm::ivec3 cell = glm::floor(m_camera.cam_pos / globals::BlockSize);
            if (chunks_manager.generator.peek(cell) == BlockType::Water)
            {
                tint = glm::vec4(.1f, .1f, .2f, 1.0f);
                fogColor = glm::vec4(0.f, 0.f, 0.05f, 1.f);
                fogStart = .0f;
                fogEnd = 10.f;
            }
            *static_cast<shaders::SolidFlatShader::PerFrameConstants*>(sb->ptr) = {
                .ViewProjection = {
                    glm::transpose(frame.projection[0] * frame.view[0]),
                    glm::transpose(frame.projection[1] * frame.view[1]),
                },
                .tint = tint,
                .fogColor = fogColor,
                .fogStart = fogStart,
                .fogEnd = fogEnd,
            };
            // defer copy
            globals::m_resources->copy_buffers.emplace_back(
                globals::m_resources->frame_buffer, *sb, dst_sb->offset);
            shader_flat_frame = *dst_sb;
            // defer suballocation deletion
            globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                std::pair(std::ref(globals::m_resources->staging_buffer), *sb));
            globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                std::pair(std::ref(globals::m_resources->frame_buffer), *dst_sb));
            if (const auto set = shaders::shader_opaque->alloc_descriptor(frame.present_index, 0))
            {
                shader_flat_frame_set = *set;
                shaders::shader_opaque->write_buffer(*set, 0, globals::m_resources->frame_buffer.buffer(),
                    dst_sb->offset, dst_sb->size, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
                shaders::shader_opaque->write_texture(*set, 1,
                    globals::m_resources->texture.image_view, globals::m_resources->sampler);
            }
        }

        if (const auto sb = globals::m_resources->staging_buffer.suballoc(
            sizeof(shaders::SolidColorShader::PerFrameConstants), 64))
        {
            const auto dst_sb = globals::m_resources->frame_buffer.suballoc(sb->size, 64);
            *static_cast<shaders::SolidColorShader::PerFrameConstants*>(sb->ptr) = {
                .ViewProjection = {
                    glm::transpose(frame.projection[0] * frame.view[0]),
                    glm::transpose(frame.projection[1] * frame.view[1]),
                }
            };
            // defer copy
            globals::m_resources->copy_buffers.emplace_back(
                globals::m_resources->frame_buffer, *sb, dst_sb->offset);
            shader_color_frame = *dst_sb;
            // defer suballocation deletion
            globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                std::pair(std::ref(globals::m_resources->staging_buffer), *sb));
            globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                std::pair(std::ref(globals::m_resources->frame_buffer), *dst_sb));
            if (const auto set = shaders::shader_color->alloc_descriptor(frame.present_index, 0))
            {
                shader_color_frame_set = *set;
                shaders::shader_color->write_buffer(*set, 0, globals::m_resources->frame_buffer.buffer(),
                    dst_sb->offset, dst_sb->size, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            }
        }

        if (const auto sb = globals::m_resources->staging_buffer.suballoc(
            sizeof(shaders::TexturedShader::PerFrameConstants), 64))
        {
            const auto dst_sb = globals::m_resources->frame_buffer.suballoc(sb->size, 64);
            *static_cast<shaders::TexturedShader::PerFrameConstants*>(sb->ptr) = {
                .ViewProjection = {
                    glm::transpose(frame.projection[0] * frame.view[0]),
                    glm::transpose(frame.projection[1] * frame.view[1]),
                }
            };
            // defer copy
            globals::m_resources->copy_buffers.emplace_back(
                globals::m_resources->frame_buffer, *sb, dst_sb->offset);
            shader_textured_frame = *dst_sb;
            // defer suballocation deletion
            globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                std::pair(std::ref(globals::m_resources->staging_buffer), *sb));
            globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                std::pair(std::ref(globals::m_resources->frame_buffer), *dst_sb));
            if (const auto set = shaders::shader_textured->alloc_descriptor(frame.present_index, 0))
            {
                shader_textured_frame_set = *set;
                shaders::shader_textured->write_buffer(*set, 0, globals::m_resources->frame_buffer.buffer(),
                    dst_sb->offset, dst_sb->size, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            }
        }

        for (auto& [k, state] : chunks_manager.m_chunks_state)
        {
            const auto& shader = (k == BlockLayer::Transparent) ? shaders::shader_transparent : shaders::shader_opaque;
            if (const auto set = shader->alloc_descriptor(frame.present_index, 1))
            {
                state.object_descriptor_set = *set;
                shader->write_buffer(*set, 0, globals::m_resources->object_buffer.buffer(),
                    state.uniform_buffer.offset, state.uniform_buffer.size,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            }
        }

        if (const auto sb = globals::m_resources->staging_buffer.suballoc(
            sizeof(shaders::SolidColorShader::PerObjectBuffer), 64))
        {
            if (const auto dst_sb = globals::m_resources->object_buffer.suballoc(sb->size, 64))
            {
                const glm::vec4 forward = glm::vec4{0, 0, -1, 1} * view;
                if (auto hit = chunks_manager.build_block_cell(m_camera.cam_pos, forward))
                {
                    *static_cast<shaders::SolidColorShader::PerObjectBuffer*>(sb->ptr) = {
                        .ObjectTransform = glm::transpose(
                            glm::gtc::translate(glm::vec3(*hit) * globals::BlockSize) *
                            glm::gtx::scale(glm::vec3(globals::BlockSize))),
                        .Color = {.1, .1, .1, 1}
                    };
                    // LOGI("hit: [%d, %d, %d]", hit->x, hit->y, hit->z);
                }
                else
                {
                    *static_cast<shaders::SolidColorShader::PerObjectBuffer*>(sb->ptr) = {
                        .ObjectTransform = glm::gtc::identity<glm::mat4>(),
                        .Color = {0, 0, 0, 1}
                    };
                }
                globals::m_resources->copy_buffers.emplace_back(
                    globals::m_resources->object_buffer, *sb, dst_sb->offset);
                m_cube.uniform_buffer = *dst_sb;
                globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                    std::pair(std::ref(globals::m_resources->object_buffer), *dst_sb));
            }
            // defer suballocation deletion
            globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                std::pair(std::ref(globals::m_resources->staging_buffer), *sb));
        }
        if (const auto set = shaders::shader_color->alloc_descriptor(frame.present_index, 1))
        {
            m_cube.object_descriptor_set = *set;
            shaders::shader_color->write_buffer(*set, 0, globals::m_resources->object_buffer.buffer(),
                m_cube.uniform_buffer.offset, m_cube.uniform_buffer.size,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        if (systems::m_server_system)
        {
            for (auto& player : systems::m_server_system->get_players())
            {
                if (const auto sb = globals::m_resources->staging_buffer.suballoc(sizeof(shaders::SolidColorShader::PerObjectBuffer), 64))
                {
                    if (const auto dst_sb = globals::m_resources->object_buffer.suballoc(sb->size, 64))
                    {
                        const glm::mat4 transform = glm::gtc::translate(player.position) *
                            glm::gtc::mat4_cast(player.rotation) *
                            glm::gtc::scale(glm::vec3(.25f)) *
                            glm::gtc::translate(glm::vec3(-.5f));
                        *static_cast<shaders::SolidColorShader::PerObjectBuffer*>(sb->ptr) = {
                            .ObjectTransform = glm::transpose(transform),
                            .Color = {1, 0, 0, 1}
                        };
                        globals::m_resources->copy_buffers.emplace_back(globals::m_resources->object_buffer, *sb, dst_sb->offset);
                        player.cube.uniform_buffer = *dst_sb;
                        globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                            std::pair(std::ref(globals::m_resources->object_buffer), *dst_sb));
                    }
                    // defer suballocation deletion
                    globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                        std::pair(std::ref(globals::m_resources->staging_buffer), *sb));
                }
                if (const auto set = shaders::shader_color->alloc_descriptor(frame.present_index, 1))
                {
                    player.cube.object_descriptor_set = *set;
                    shaders::shader_color->write_buffer(*set, 0, globals::m_resources->object_buffer.buffer(),
                        player.cube.uniform_buffer.offset, player.cube.uniform_buffer.size,
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                }
            }
        }
        if (systems::m_client_system)
        {
            for (auto& player : systems::m_client_system->get_players())
            {
                if (const auto sb = globals::m_resources->staging_buffer.suballoc(sizeof(shaders::SolidColorShader::PerObjectBuffer), 64))
                {
                    if (const auto dst_sb = globals::m_resources->object_buffer.suballoc(sb->size, 64))
                    {
                        const glm::mat4 transform = glm::gtc::translate(player.position) *
                            glm::gtc::mat4_cast(player.rotation) *
                            glm::gtc::scale(glm::vec3(.25f)) *
                            glm::gtc::translate(glm::vec3(-.5f));
                        *static_cast<shaders::SolidColorShader::PerObjectBuffer*>(sb->ptr) = {
                            .ObjectTransform = glm::transpose(transform),
                            .Color = {1, 0, 0, 1}
                        };
                        globals::m_resources->copy_buffers.emplace_back(globals::m_resources->object_buffer, *sb, dst_sb->offset);
                        player.cube.uniform_buffer = *dst_sb;
                        globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                            std::pair(std::ref(globals::m_resources->object_buffer), *dst_sb));
                    }
                    // defer suballocation deletion
                    globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                        std::pair(std::ref(globals::m_resources->staging_buffer), *sb));
                }
                if (const auto set = shaders::shader_color->alloc_descriptor(frame.present_index, 1))
                {
                    player.cube.object_descriptor_set = *set;
                    shaders::shader_color->write_buffer(*set, 0, globals::m_resources->object_buffer.buffer(),
                        player.cube.uniform_buffer.offset, player.cube.uniform_buffer.size,
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
                }
            }
        }
        // update OBJ meshes
        for (auto& geo : m_obj_meshes)
        {
            if (const auto sb = globals::m_resources->staging_buffer.suballoc(sizeof(shaders::TexturedShader::PerObjectBuffer), 64))
            {
                if (const auto dst_sb = globals::m_resources->object_buffer.suballoc(sb->size, 64))
                {
                    const glm::mat4 transform = glm::gtc::translate(glm::vec3(0, 5, 0)) *
                        glm::gtc::scale(glm::vec3(20.f));
                    *static_cast<shaders::TexturedShader::PerObjectBuffer*>(sb->ptr) = {
                        .ObjectTransform = glm::transpose(transform),
                    };
                    globals::m_resources->copy_buffers.emplace_back(globals::m_resources->object_buffer, *sb, dst_sb->offset);
                    geo.uniform_buffer = *dst_sb;
                    globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                        std::pair(std::ref(globals::m_resources->object_buffer), *dst_sb));
                }
                // defer suballocation deletion
                globals::m_resources->delete_buffers.emplace(frame.timeline_value,
                    std::pair(std::ref(globals::m_resources->staging_buffer), *sb));
            }
            if (const auto set = shaders::shader_textured->alloc_descriptor(frame.present_index, 1))
            {
                geo.object_descriptor_set = *set;
                shaders::shader_textured->write_buffer(*set, 0, globals::m_resources->object_buffer.buffer(),
                    geo.uniform_buffer.offset, geo.uniform_buffer.size,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            }
        }

        if (const auto set = shaders::shader_textured->alloc_descriptor(frame.present_index, 2))
        {
            shader_textured_material_set = *set;
            shaders::shader_textured->write_texture(*set, 0,
                m_obj_texture.image_view, globals::m_resources->sampler);
        }
    }
    void tick(const float dt) noexcept
    {
        m_player.character->PostSimulation(0.1);
        m_camera.cam_pos = glm::gtc::make_vec3(m_player.character->GetPosition().mF32);
        const bool new_on_ground = m_player.character->GetGroundState() == JPH::Character::EGroundState::OnGround;
        if (m_player.on_ground != new_on_ground)
        {
            m_player.on_ground = new_on_ground;
            if (m_player.on_ground)
            {
                // play landing sound only when falling
                if (m_player.character->GetLinearVelocity().GetY() < 0)
                    systems::m_audio_system->play_sound(std::format("walk/Sound {:02d}.wav", glm::gtc::linearRand(22, 23)));
                m_player.walk_start = glm::gtc::make_vec3(m_player.character->GetGroundPosition().mF32);
            }
        }
        else
        {
            if (m_player.on_ground)
            {
                const auto current_pos = glm::gtc::make_vec3(m_player.character->GetGroundPosition().mF32);
                const auto distance_walked = glm::distance(m_player.walk_start, current_pos);
                if (distance_walked > 0.5f)
                {
                    m_player.walk_start = current_pos;
                    // play step sound
                    systems::m_audio_system->play_sound(std::format("walk/Sound {:02d}.wav", glm::gtc::linearRand(1, 21)));
                }
            }
        }
    }
    void render(const float dt, VkCommandBuffer cmd) noexcept
    {
        if (chunks_manager.m_chunks_state.size() > 0)
        {
            std::vector<std::pair<std::shared_ptr<vk::ShaderModule>, chunksman::ChunksState>> layers{};
            if (chunks_manager.m_chunks_state.contains(BlockLayer::Solid))
            {
                layers.emplace_back(shaders::shader_opaque, chunks_manager.m_chunks_state[BlockLayer::Solid]);
            }
            if (chunks_manager.m_chunks_state.contains(BlockLayer::Transparent))
            {
                layers.emplace_back(shaders::shader_transparent, chunks_manager.m_chunks_state[BlockLayer::Transparent]);
            }
            for (const auto& [shader, state] : layers)
            {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline());

                const std::array vertex_buffers{globals::m_resources->vertex_buffer.buffer()};
                const std::array vertex_buffers_offset{VkDeviceSize{0ull}};
                vkCmdBindVertexBuffers(cmd, 0, static_cast<uint32_t>(vertex_buffers.size()),
                    vertex_buffers.data(), vertex_buffers_offset.data());

                const std::array sets{shader_flat_frame_set, state.object_descriptor_set};
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    shader->layout(), 0, sets.size(), sets.data(), 0, nullptr);

                vkCmdDrawIndirect(cmd, globals::m_resources->args_buffer.buffer(),
                    state.args_buffer.offset, state.draw_count, sizeof(VkDrawIndirectCommand));
            }
        }

        // draw cube
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shaders::shader_color->pipeline());
            vkCmdSetLineWidth(cmd, 2.f);
            vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
            //vkCmdSetPolygonModeEXT(cmd, VK_POLYGON_MODE_LINE);

            const std::array vertex_buffers{globals::m_resources->vertex_buffer.buffer()};
            const std::array vertex_buffers_offset{m_cube.vertex_buffer.offset};
            vkCmdBindVertexBuffers(cmd, 0, static_cast<uint32_t>(vertex_buffers.size()),
                vertex_buffers.data(), vertex_buffers_offset.data());

            const std::array sets{shader_color_frame_set, m_cube.object_descriptor_set};
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                shaders::shader_color->layout(), 0, sets.size(), sets.data(), 0, nullptr);

            vkCmdDraw(cmd, m_cube.vertex_count, 1, 0, 0);

            if (systems::m_server_system)
            {
                for (const auto& player : systems::m_server_system->get_players())
                {
                    const std::array sets{shader_color_frame_set, player.cube.object_descriptor_set};
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        shaders::shader_color->layout(), 0, sets.size(), sets.data(), 0, nullptr);

                    vkCmdDraw(cmd, player.cube.vertex_count, 1, 0, 0);
                }
            }
            if (systems::m_client_system)
            {
                for (const auto& player : systems::m_client_system->get_players())
                {
                    const std::array sets{shader_color_frame_set, player.cube.object_descriptor_set};
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        shaders::shader_color->layout(), 0, sets.size(), sets.data(), 0, nullptr);

                    vkCmdDraw(cmd, player.cube.vertex_count, 1, 0, 0);
                }
            }
        }

        // draw OBJ
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shaders::shader_textured->pipeline());

            const std::array vertex_buffers{globals::m_resources->vertex_buffer.buffer()};

            for (const auto& geo : m_obj_meshes)
            {
                const std::array vertex_buffers_offset{geo.vertex_buffer.offset};
                vkCmdBindVertexBuffers(cmd, 0, static_cast<uint32_t>(vertex_buffers.size()),
                    vertex_buffers.data(), vertex_buffers_offset.data());

                const std::array sets{shader_textured_frame_set, geo.object_descriptor_set, shader_textured_material_set};
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    shaders::shader_textured->layout(), 0, sets.size(), sets.data(), 0, nullptr);

                vkCmdDraw(cmd, geo.vertex_count, 1, 0, 0);
           }
        }
    }
};

}
