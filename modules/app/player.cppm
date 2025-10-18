module;
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/Character.h>
#include <array>

export module ce.app:player;
import glm;
import :physics;

export namespace ce::app::player
{

struct PlayerState
{
    bool dragging = false;
    glm::ivec2 drag_start = { 0, 0 };
    float cam_fov = 90.f;
    glm::vec2 cam_start = {0, 0};
    glm::vec2 cam_angles = { 0, 0 };
    glm::vec3 cam_pos = { 0, 100, 0 };
    glm::ivec3 cam_sector = { 0, 0, 0 };
    std::array<bool, 256> keys{false};
    JPH::Ref<JPH::Character> character;
};

}