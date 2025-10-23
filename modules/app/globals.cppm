module;
#include <cstdint>
export module ce.app:globals;
import :resources;

export namespace ce::app::globals
{
std::shared_ptr<resources::VulkanResources> m_resources;
// Size of a block in meters
constexpr float BlockSize = 0.5f;
// Number of blocks per chunk
constexpr uint32_t ChunkSize = 32;
constexpr uint32_t ChunkRings = 4;
}
