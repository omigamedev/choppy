module;
#include <cstdint>
#include <PerlinNoise.hpp>
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
    uint32_t size{0};
    glm::ivec3 sector{0};
    std::vector<Block> blocks;
    bool empty = true;
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
	siv::PerlinNoise perlin{ std::random_device{} };

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
                    // const float terrain_height = cosf(nc.x * 1.f) * sinf(nc.z * 2.f) * static_cast<float>(m_ground_height);
                    const float terrain_height = perlin.octave2D(nc.x, nc.z, 4) * static_cast<float>(m_ground_height);
                    //const float terrain_height = cell.z;//static_cast<float>(m_ground_height);
                    BlockType block = BlockType::Air;
                     if (cell.y < 0)
                         block = BlockType::Water;
                    if (cell.y <= terrain_height)
                        block = BlockType::Dirt;
                    //const BlockType block = cell.y <= terrain_height ? BlockType::Dirt : BlockType::Air;
                    tmp.emplace_back(block);
                }
            }
        }

        bool full = false;
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
                    if (C == BlockType::Water)
                    {
                        mask |= 1;//(tmp[(y + 2) * pow(sz, 2) + (z + 1) * sz + x + 1] == BlockType::Air) << 0;
                        mask |= (tmp[(y + 0) * pow(sz, 2) + (z + 1) * sz + x + 1] == BlockType::Air) << 1;
                        mask |= (tmp[(y + 1) * pow(sz, 2) + (z + 2) * sz + x + 1] == BlockType::Air) << 2;
                        mask |= (tmp[(y + 1) * pow(sz, 2) + (z + 0) * sz + x + 1] == BlockType::Air) << 3;
                        mask |= (tmp[(y + 1) * pow(sz, 2) + (z + 1) * sz + x + 2] == BlockType::Air) << 4;
                        mask |= (tmp[(y + 1) * pow(sz, 2) + (z + 1) * sz + x + 0] == BlockType::Air) << 5;
                    }
                    else
                    {
                        mask |= (tmp[(y + 2) * pow(sz, 2) + (z + 1) * sz + x + 1] != C) << 0;
                        mask |= (tmp[(y + 0) * pow(sz, 2) + (z + 1) * sz + x + 1] != C) << 1;
                        mask |= (tmp[(y + 1) * pow(sz, 2) + (z + 2) * sz + x + 1] != C) << 2;
                        mask |= (tmp[(y + 1) * pow(sz, 2) + (z + 0) * sz + x + 1] != C) << 3;
                        mask |= (tmp[(y + 1) * pow(sz, 2) + (z + 1) * sz + x + 2] != C) << 4;
                        mask |= (tmp[(y + 1) * pow(sz, 2) + (z + 1) * sz + x + 0] != C) << 5;
                    }
                    const BlockType type = mask == 0 ? BlockType::Air : C;
                    blocks.emplace_back(type, static_cast<Block::Mask>(mask));
                    full |= type != BlockType::Air;
                }
            }
        }

        return ChunkData{.size = m_size, .sector = sector, .blocks = std::move(blocks), .empty = !full};
    }
};
}
