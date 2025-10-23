module;
#include <cstdint>
#include <algorithm>
#include <ranges>
#include <vector>
#include <cstddef>
#include <numeric>
export module ce.app:utils;
import glm;

export namespace ce::app::utils
{
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
template <
    std::ranges::random_access_range R,
    typename Comp = std::ranges::less,
    typename Proj = std::identity
>
auto sorted_view(R& r, Comp comp = {}, Proj proj = {})
{
   using size_type = std::size_t;

    // Build and sort indices
    std::vector<size_type> idx(std::ranges::size(r));
    std::iota(idx.begin(), idx.end(), size_type{0});

    std::ranges::stable_sort(idx,
        [&](size_type i, size_type j)
        {
            using std::ranges::begin;
            return std::invoke(comp,
                std::invoke(proj, *(begin(r) + i)),
                std::invoke(proj, *(begin(r) + j)));
        });

    // Keep indices alive inside the returned view
    auto ownedIdx = std::ranges::owning_view{std::move(idx)};

    using Range = std::remove_reference_t<R>;
    Range* ptr = std::addressof(r); // r must outlive the returned view

    // Return a lazy view of elements in sorted order
    return std::move(ownedIdx)
        | std::views::transform([ptr](size_type i) -> decltype(auto)
        {
            using std::ranges::begin;
            return *(begin(*ptr) + i);
        });
}
[[nodiscard]] std::vector<std::tuple<glm::ivec3, float, glm::ivec3>> traverse_3d_dda(const glm::vec3& origin,
    const glm::vec3& direction, const glm::vec3& grid_origin_reference, const float max_t, const float BlockSize) noexcept
{
    std::vector<std::tuple<glm::ivec3, float, glm::ivec3>> traversed_voxels;
    // Grid coordinate of the ray's origin relative to the grid reference (0,0,0)
    const glm::vec3 relative_origin = origin - grid_origin_reference;

    // Direction sign for stepping (+1 or -1)
    const glm::ivec3 step{
        (direction.x >= 0.0f) ? 1 : -1,
        (direction.y >= 0.0f) ? 1 : -1,
        (direction.z >= 0.0f) ? 1 : -1,
    };

    constexpr float infinity = std::numeric_limits<float>::max();
    const glm::vec3 t_delta{
        (glm::abs(direction.x) < 1e-6f) ? infinity : BlockSize / glm::abs(direction.x),
        (glm::abs(direction.y) < 1e-6f) ? infinity : BlockSize / glm::abs(direction.y),
        (glm::abs(direction.z) < 1e-6f) ? infinity : BlockSize / glm::abs(direction.z),
    };

    // Helper lambda to calculate initial t_max for one axis
    const auto calculate_initial_t = [&](const float origin_comp,
        const float direction_comp, const int step_comp, const float grid_ref_comp) -> float
    {
        if (glm::abs(direction_comp) < 1e-6f) return max_t;

        const float rel_origin_comp = origin_comp - grid_ref_comp;
        const float cell_start = glm::floor(rel_origin_comp / BlockSize) * BlockSize;
        // Next boundary world coordinate
        const float boundary = (step_comp > 0) ? (cell_start + BlockSize) : cell_start;
        const float t = (boundary - rel_origin_comp) / direction_comp;
        // Correction for starting exactly on a boundary
        if (t < 1e-6f && step_comp < 0)
        {
            return BlockSize / glm::abs(direction_comp);
        }
        return glm::max(t, 0.0f);
    };

    // Calculate initial t_max for X, Y, Z
    // t_max: parametric distance 't' to the first grid plane intersection for each axis
    glm::vec3 t_max{
        calculate_initial_t(origin.x, direction.x, step.x, grid_origin_reference.x),
        calculate_initial_t(origin.y, direction.y, step.y, grid_origin_reference.y),
        calculate_initial_t(origin.z, direction.z, step.z, grid_origin_reference.z),
    };

    glm::ivec3 current_cell = glm::ivec3(glm::floor(relative_origin / BlockSize));

    // Initial cell is recorded at t=0 with a (0,0,0) normal (since no face was hit yet)
    traversed_voxels.emplace_back(current_cell, 0.0f, glm::ivec3(0));

    while (glm::gtx::compMin(t_max) < max_t)
    {
        glm::ivec3 normal(0); // Face normal for the plane being crossed
        // Determine the axis (and thus the plane) that will be crossed next
        if (t_max.x < t_max.y)
        {
            if (t_max.x < t_max.z)
            {
                // X-axis is the shortest step
                current_cell.x += step.x;
                normal.x = -step.x; // Normal points opposite to the ray step direction
                traversed_voxels.emplace_back(current_cell, t_max.x, normal);
                t_max.x += t_delta.x;
            }
            else
            {
                // Z-axis is the shortest step (or equal to X)
                current_cell.z += step.z;
                normal.z = -step.z;
                traversed_voxels.emplace_back(current_cell, t_max.z, normal);
                t_max.z += t_delta.z;
            }
        }
        else
        {
            if (t_max.y < t_max.z)
            {
                // Y-axis is the shortest step
                current_cell.y += step.y;
                normal.y = -step.y;
                traversed_voxels.emplace_back(current_cell, t_max.y, normal);
                t_max.y += t_delta.y;
            }
            else
            {
                // Z-axis is the shortest step (or equal to Y)
                current_cell.z += step.z;
                normal.z = -step.z;
                traversed_voxels.emplace_back(current_cell, t_max.z, normal);
                t_max.z += t_delta.z;
            }
        }
    }

    return traversed_voxels;
}
}
