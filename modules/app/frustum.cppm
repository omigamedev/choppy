module;
#include <array>

export module ce.app:frustum;
import glm;

export namespace ce::app
{
/// @brief Represents a single plane in 3D space, defined by Ax + By + Cz + D = 0.
struct Plane
{
    glm::vec3 normal{0.f, 1.f, 0.f}; // A, B, C
    float distance = 0.f;           // D

    /// @brief Normalizes the plane equation.
    void normalize()
    {
        const float mag = glm::length(normal);
        if (mag > 1e-6f)
        {
            normal /= mag;
            distance /= mag;
        }
    }
};

/// @brief Represents an Axis-Aligned Bounding Box.
struct AABB
{
    glm::vec3 min{0.f};
    glm::vec3 max{0.f};
};

/// @brief Represents the view frustum defined by 6 planes.
class Frustum
{
public:
    /// @brief Extracts the 6 frustum planes from a combined view-projection matrix.
    /// @param matrix The view-projection matrix.
    void update(const glm::mat4& matrix)
    {
        // Left plane
        m_planes[0].normal.x = matrix[0][3] + matrix[0][0];
        m_planes[0].normal.y = matrix[1][3] + matrix[1][0];
        m_planes[0].normal.z = matrix[2][3] + matrix[2][0];
        m_planes[0].distance = matrix[3][3] + matrix[3][0];

        // Right plane
        m_planes[1].normal.x = matrix[0][3] - matrix[0][0];
        m_planes[1].normal.y = matrix[1][3] - matrix[1][0];
        m_planes[1].normal.z = matrix[2][3] - matrix[2][0];
        m_planes[1].distance = matrix[3][3] - matrix[3][0];

        // Bottom plane
        m_planes[2].normal.x = matrix[0][3] + matrix[0][1];
        m_planes[2].normal.y = matrix[1][3] + matrix[1][1];
        m_planes[2].normal.z = matrix[2][3] + matrix[2][1];
        m_planes[2].distance = matrix[3][3] + matrix[3][1];

        // Top plane
        m_planes[3].normal.x = matrix[0][3] - matrix[0][1];
        m_planes[3].normal.y = matrix[1][3] - matrix[1][1];
        m_planes[3].normal.z = matrix[2][3] - matrix[2][1];
        m_planes[3].distance = matrix[3][3] - matrix[3][1];

        // Near plane (for right-handed, Z-up, 0-to-1 depth like Vulkan)
        m_planes[4].normal.x = matrix[0][2];
        m_planes[4].normal.y = matrix[1][2];
        m_planes[4].normal.z = matrix[2][2];
        m_planes[4].distance = matrix[3][2];

        // Far plane
        m_planes[5].normal.x = matrix[0][3] - matrix[0][2];
        m_planes[5].normal.y = matrix[1][3] - matrix[1][2];
        m_planes[5].normal.z = matrix[2][3] - matrix[2][2];
        m_planes[5].distance = matrix[3][3] - matrix[3][2];

        for (auto& plane : m_planes)
        {
            plane.normalize();
        }
    }

    /// @brief Checks if an AABB is inside or intersects the frustum.
    /// @return True if the AABB is visible, false if it's completely outside.
    [[nodiscard]] bool is_box_visible(const AABB& box) const
    {
        for (const auto& [normal, distance] : m_planes)
        {
            const glm::vec3 p_vertex = box.min + (glm::vec3(glm::greaterThan(normal, glm::vec3(0.0f))) * (box.max - box.min));
            if (glm::dot(normal, p_vertex) + distance < 0.0f)
            {
                return false; // Box is completely outside this plane
            }
        }
        return true; // Box is visible
    }

private:
    std::array<Plane, 6> m_planes;
};
}