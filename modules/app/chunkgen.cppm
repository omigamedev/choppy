module;
#include <cstdint>
#include <vector>

export module ce.app.chunkgen;
import ce.app.utils;
import glm;

export namespace ce::app
{
enum class BlockType : uint8_t
{
    Air,
    Water,
    Grass,
    Dirt,
};
struct Block final
{
    BlockType type;
    enum class Mask : uint8_t { U = 1, D = 2, F = 4, B = 8, R = 16, L = 32 } face_mask;
};
struct ChunkData final : NoCopy
{
    ChunkData(const uint32_t size, const glm::ivec3& sector, const std::vector<Block>&& blocks)
        : size(size),
          sector(sector),
          blocks(blocks)
    {
    }

    uint32_t size;
    glm::ivec3 sector;
    std::vector<Block> blocks;
};
class ChunkGenerator
{
public:
    virtual ~ChunkGenerator() = default;
    [[nodiscard]] virtual ChunkData generate(glm::ivec3 sector) const noexcept = 0;
};
class FlatGenerator final : public ChunkGenerator
{
    uint32_t m_size = 0;
    uint32_t m_ground_height = 0;
public:
    explicit FlatGenerator(const uint32_t size, const uint32_t ground_height) noexcept
        : m_size(size), m_ground_height(ground_height) { }
    [[nodiscard]] ChunkData generate([[maybe_unused]] const glm::ivec3 sector) const noexcept override
    {
        const int32_t ssz = static_cast<int32_t>(m_size);
        std::vector<BlockType> tmp;
        tmp.reserve(pow(ssz + 2, 3));
        for (int32_t y = -1; y < ssz + 1; ++y)
        {
            for (int32_t z = -1; z < ssz + 1; ++z)
            {
                for (int32_t x = -1; x < ssz + 1; ++x)
                {
                    const glm::ivec3 loc{x, y, -z};
                    const glm::ivec3 cell = loc + sector * ssz;
                    const glm::vec3 nc = glm::vec3(cell) / static_cast<float>(ssz);
                    const float terrain_height = cosf(nc.x * 1.f) * sinf(nc.z * 2.f) * 5.f;
                    //const float terrain_height = cosf(nc.z * 1.f) * 10.f;
                    const BlockType block = cell.y <= terrain_height ? BlockType::Dirt : BlockType::Air;
                    tmp.emplace_back(block);
                }
            }
        }

        std::vector<Block> blocks;
        blocks.reserve(pow(ssz, 3));
        for (uint32_t y = 0; y < m_size; ++y)
        {
            for (uint32_t z = 0; z < m_size; ++z)
            {
                for (uint32_t x = 0; x < m_size; ++x)
                {
                    const int32_t sz = static_cast<int32_t>(m_size + 2);
                    const auto C = tmp[(y + 1) * pow(sz, 2) + (z + 1) * sz + x + 1];
                    uint8_t mask = 0;
                    mask |= (tmp[(y + 2) * pow(sz, 2) + (z + 1) * sz + x + 1] == BlockType::Air) << 0;
                    mask |= (tmp[(y + 0) * pow(sz, 2) + (z + 1) * sz + x + 1] == BlockType::Air) << 1;
                    mask |= (tmp[(y + 1) * pow(sz, 2) + (z + 2) * sz + x + 1] == BlockType::Air) << 2;
                    mask |= (tmp[(y + 1) * pow(sz, 2) + (z + 0) * sz + x + 1] == BlockType::Air) << 3;
                    mask |= (tmp[(y + 1) * pow(sz, 2) + (z + 1) * sz + x + 2] == BlockType::Air) << 4;
                    mask |= (tmp[(y + 1) * pow(sz, 2) + (z + 1) * sz + x + 0] == BlockType::Air) << 5;
                    const BlockType type = mask == 0 ? BlockType::Air : C;
                    blocks.emplace_back(type, static_cast<Block::Mask>(mask));
                }
            }
        }

        return ChunkData{m_size, sector, std::move(blocks)};
    }
};
}
