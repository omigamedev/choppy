module;
#include <cstdint>
#include <PerlinNoise.hpp>
#include <unordered_map>
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
const char* to_string(const BlockType b)
{
    switch (b)
    {
    case BlockType::Air: return  "Air";
    case BlockType::Water: return "Water";
    case BlockType::Grass: return "Grass";
    case BlockType::Dirt: return "Dirt";
    default: return "Unknown";
    }
}
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

struct IVec3Hash
{
    size_t operator()(const glm::ivec3& v) const noexcept
    {
        // 64-bit FNV-1a style hash
        const uint64_t x = static_cast<uint64_t>(v.x);
        const uint64_t y = static_cast<uint64_t>(v.y);
        const uint64_t z = static_cast<uint64_t>(v.z);

        uint64_t h = 1469598103934665603ULL; // FNV offset
        h ^= x; h *= 1099511628211ULL;
        h ^= y; h *= 1099511628211ULL;
        h ^= z; h *= 1099511628211ULL;

        return static_cast<size_t>(h);
    }
};
struct U8Vec3Hash
{
    size_t operator()(const glm::u8vec3& v) const noexcept
    {
        // Pack x,y,z into 24 bits: [00000000|zzzzzzzz|yyyyyyyy|xxxxxxxx]
        uint32_t packed = (uint32_t(v.x)) |
                          (uint32_t(v.y) << 8) |
                          (uint32_t(v.z) << 16);

        // Promote to size_t so it works with 32- or 64-bit platforms
        return static_cast<size_t>(packed);
    }
};

class ChunkGenerator
{
public:
    virtual ~ChunkGenerator() = default;
    [[nodiscard]] virtual ChunkData generate(const glm::ivec3& sector) const noexcept = 0;
};
class FlatGenerator final : public ChunkGenerator
{
    uint32_t m_chunk_size = 0;
    uint32_t m_ground_height = 0;
	// siv::PerlinNoise perlin{ std::random_device{} };
	siv::PerlinNoise perlin{ 1 };
    std::unordered_map<glm::ivec3, std::unordered_map<glm::u8vec3, BlockType, U8Vec3Hash>, IVec3Hash> m_edits;

public:
    explicit FlatGenerator(const uint32_t size, const uint32_t ground_height) noexcept
        : m_chunk_size(size), m_ground_height(ground_height) { }
    [[nodiscard]] BlockType peek(const glm::ivec3 cell) const noexcept
    {
        const int32_t ssz = static_cast<int32_t>(m_chunk_size);
        const glm::ivec3 sector = glm::floor(glm::vec3(cell) / ssz);
        const glm::u8vec3 local_cell = cell - sector * ssz;
        const auto sector_it =  m_edits.find(sector);
        if (sector_it != m_edits.end())
        {
            const auto cell_it = sector_it->second.find(local_cell);
            if (cell_it != sector_it->second.end())
            {
                return cell_it->second;
            }
        }
        const glm::vec3 nc = glm::vec3(cell) / static_cast<float>(ssz);
        const float terrain_height = perlin.octave2D(nc.x, nc.z, 4) * static_cast<float>(m_ground_height);
        BlockType block = BlockType::Air;
        if (cell.y < 0)
            block = BlockType::Water;
        if (cell.y <= terrain_height)
            block = BlockType::Dirt;
        return block;
    }
    void edit(const glm::ivec3& sector, const glm::u8vec3& local_cell, const BlockType block_type) noexcept
    {
        m_edits[sector][local_cell] = block_type;
    }
    [[nodiscard]] ChunkData generate(const glm::ivec3& sector) const noexcept override
    {
        const int32_t ssz = static_cast<int32_t>(m_chunk_size);
        std::vector<BlockType> tmp;
        tmp.reserve(pow(ssz + 2, 3));
        for (int32_t y = -1; y < ssz + 1; ++y)
        {
            for (int32_t z = -1; z < ssz + 1; ++z)
            {
                for (int32_t x = -1; x < ssz + 1; ++x)
                {
                    const glm::ivec3 loc{x, y, z};
                    const glm::ivec3 cell = loc + sector * ssz;
                    tmp.emplace_back(peek(cell));
                }
            }
        }

        bool full = false;
        std::vector<Block> blocks;
        blocks.reserve(pow(ssz, 3));
        for (uint32_t y = 0; y < m_chunk_size; ++y)
        {
            for (uint32_t z = 0; z < m_chunk_size; ++z)
            {
                for (uint32_t x = 0; x < m_chunk_size; ++x)
                {
                    const int32_t sz = static_cast<int32_t>(m_chunk_size + 2);
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

        return ChunkData{.size = m_chunk_size, .sector = sector, .blocks = std::move(blocks), .empty = !full};
    }
};
}
