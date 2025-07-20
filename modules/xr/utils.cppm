module;
#include <cmath>
#include <openxr/openxr.h>

export module ce.xr.utils;
import glm;

export namespace ce::xr::utils
{
[[nodiscard]] glm::mat4 pose_to_mat4(const XrPosef& pose) noexcept
{
    const glm::quat orientation(
        pose.orientation.w,
        pose.orientation.x,
        pose.orientation.y,
        pose.orientation.z
    );
    const glm::vec3 position(
        pose.position.x,
        pose.position.y,
        pose.position.z
    );
    const glm::mat4 rotationMatrix = glm::gtc::mat4_cast(orientation);
    const glm::mat4 translationMatrix = glm::gtc::translate(glm::mat4(1.0f), position);
    return glm::inverse(translationMatrix * rotationMatrix);
}
[[nodiscard]] glm::mat4 projection(const XrFovf& fov, const float nearZ, const float farZ) noexcept
{
    const float tanLeft   = std::tanf(fov.angleLeft);
    const float tanRight  = std::tanf(fov.angleRight);
    const float tanDown   = std::tanf(fov.angleDown);
    const float tanUp     = std::tanf(fov.angleUp);

    const float tanWidth  = tanRight - tanLeft;
    const float tanHeight = tanUp - tanDown;

    glm::mat4 proj = glm::mat4(0.0f);

    proj[0][0] = 2.0f / tanWidth;
    proj[1][1] = 2.0f / tanHeight;
    proj[2][0] = (tanRight + tanLeft) / tanWidth;
    proj[2][1] = (tanUp + tanDown) / tanHeight;
    proj[2][2] = (farZ + nearZ) / (nearZ - farZ);
    proj[2][3] = -1.0f;
    proj[3][2] = (2.0f * farZ * nearZ) / (nearZ - farZ);

    return proj;
}
}
