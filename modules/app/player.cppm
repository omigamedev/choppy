module;
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/Character.h>
#include <array>

export module ce.app:player;
import glm;
import :physics;
import :resources;

export namespace ce::app::player
{
struct PlayerCamera
{
    bool dragging = false;
    glm::ivec2 drag_start = { 0, 0 };
    float cam_fov = 90.f;
    glm::vec2 cam_start = {0, 0};
    glm::vec2 cam_angles = { 0, 0 };
    glm::vec3 cam_pos = { 0, 100, 0 };
    glm::ivec3 cam_sector = { 0, 0, 0 };
};
struct PlayerState
{
    uint32_t id = 0;
    JPH::Ref<JPH::Character> character;
    resources::Geometry cube[3];
    glm::vec3 walk_start{};
    bool on_ground = false;
    std::array<glm::vec3, 3> position{};
    std::array<glm::quat, 3> rotation{};
    std::array<glm::vec3, 3> velocity{};
    void destroy() noexcept
    {
        if (character)
            character->RemoveFromPhysicsSystem();
        character = nullptr;
    }
};
}
