module;
#include <vector>
#include <tuple>
#include <memory>
#include <map>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <tracy/Tracy.hpp>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#elifdef _WIN32
#include <windows.h>
#define LOGE(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

export module ce.app:resources;
import glm;
import ce.vk;
import ce.vk.buffer;
import ce.vk.texture;
import :utils;

export namespace ce::app::resources
{
struct VulkanResources;
struct Geometry : utils::NoCopy
{
    VulkanResources* vkr = nullptr;
    VkDescriptorSet object_descriptor_set = VK_NULL_HANDLE;
    vk::BufferSuballocation vertex_buffer{};
    vk::BufferSuballocation uniform_buffer{};
    uint32_t vertex_count = 0;
    Geometry() = default;
    Geometry(const Geometry& other) = delete;
    Geometry(Geometry&& other) noexcept
        : vkr(other.vkr),
          object_descriptor_set(other.object_descriptor_set),
          vertex_buffer(std::move(other.vertex_buffer)),
          uniform_buffer(std::move(other.uniform_buffer)),
          vertex_count(other.vertex_count)
    {
    }
    Geometry& operator=(const Geometry& other) = delete;
    Geometry& operator=(Geometry&& other) noexcept
    {
        if (this == &other)
            return *this;
        vkr = other.vkr;
        object_descriptor_set = other.object_descriptor_set;
        vertex_buffer = std::move(other.vertex_buffer);
        uniform_buffer = std::move(other.uniform_buffer);
        vertex_count = other.vertex_count;
        other.object_descriptor_set = VK_NULL_HANDLE;
        other.vertex_count = 0;
        return *this;
    }
    Geometry(VulkanResources* vkr) : vkr(vkr) {}
    ~Geometry();
};
struct VulkanResources : utils::NoCopy
{
    std::multimap<uint64_t, std::pair<vk::Buffer&, const vk::BufferSuballocation>> delete_buffers;
    std::vector<std::tuple<vk::Buffer&, const vk::BufferSuballocation, VkDeviceSize /*dst_offset*/>> copy_buffers;
    vk::Buffer staging_buffer;
    vk::Buffer vertex_buffer;
    vk::Buffer frame_buffer;
    vk::Buffer object_buffer;
    vk::Buffer args_buffer;
    vk::texture::Texture texture;
    VkSampler sampler = VK_NULL_HANDLE;
    bool create_buffers(const std::shared_ptr<vk::Context>& vk)
    {
        ZoneScoped;
        // Create and upload vertex buffer
        vertex_buffer = vk::Buffer(vk, "ChunksVertexBuffer");
        if (!vertex_buffer.create(1024 * (1 << 20),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE))
        {
            LOGE("Failed to create chunks vertex buffer");
            return false;
        }

        frame_buffer = vk::Buffer(vk, "ChunksPerFrameBuffer");
        if (!frame_buffer.create(16 * (1 << 20),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE))
        {
            LOGE("Failed to create cube vertex buffer");
            return false;
        }

        object_buffer = vk::Buffer(vk, "ChunksPerObjectBuffer");
        if (!object_buffer.create(64 * (1 << 20),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE))
        {
            LOGE("Failed to create cube vertex buffer");
            return false;
        }

        staging_buffer = vk::Buffer(vk, "StagingBuffer");
        if (!staging_buffer.create(128 * (1 << 20),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
            VMA_ALLOCATION_CREATE_MAPPED_BIT))
        {
            LOGE("Failed to create cube vertex buffer");
            return false;
        }

        args_buffer = vk::Buffer(vk, "CubeArgsBuffer");
        constexpr uint32_t args_buffer_size = sizeof(VkDrawIndirectCommand) * 30'000;
        if (!args_buffer.create(args_buffer_size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE))
        {
            LOGE("Failed to create cube vertex buffer");
            return false;
        }
        return true;
    }
    void garbage_collect(const uint64_t timeline_value) noexcept
    {
        ZoneScoped;
        std::multimap<uint64_t, std::pair<vk::Buffer&, const vk::BufferSuballocation>> not_deleted;
        uint32_t removed_count = 0;
        for (auto& [timeline, buffer_pair] : delete_buffers)
        {
            if (timeline_value >= timeline)
            {
                auto& [buffer, sb] = buffer_pair;
                buffer.subfree(sb);
                removed_count++;
            }
            else
            {
                not_deleted.emplace(timeline, buffer_pair);
            }
        }
        delete_buffers = std::move(not_deleted);
    }
    template<typename ShaderType> [[nodiscard]] Geometry create_cube() noexcept
    {
        ZoneScoped;
        Geometry cube{this};
        // Define the 8 vertices of the cube with unique colors for interpolation
        constexpr std::array<typename ShaderType::VertexInput, 8> points = {{
            // {position},
            {{0, 0, 1, 1.0f}}, // 0: Front-Bottom-Left,  Red
            {{1, 0, 1, 1.0f}}, // 1: Front-Bottom-Right, Green
            {{1, 1, 1, 1.0f}}, // 2: Front-Top-Right,    Blue
            {{0, 1, 1, 1.0f}}, // 3: Front-Top-Left,     Yellow
            {{0, 0, 0, 1.0f}}, // 4: Back-Bottom-Left,   Magenta
            {{1, 0, 0, 1.0f}}, // 5: Back-Bottom-Right,  Cyan
            {{1, 1, 0, 1.0f}}, // 6: Back-Top-Right,     White
            {{0, 1, 0, 1.0f}}, // 7: Back-Top-Left,      Gray
        }};

        // Define the 36 indices for the 12 triangles of the cube
        constexpr std::array<uint32_t, 36> indices = {
            // Front face (+Z)
            0, 1, 2,   2, 3, 0,
            // Back face (-Z)
            4, 7, 6,   6, 5, 4,
            // Top face (+Y)
            3, 2, 6,   6, 7, 3,
            // Bottom face (-Y)
            4, 5, 1,   1, 0, 4,
            // Right face (+X)
            1, 5, 6,   6, 2, 1,
            // Left face (-X)
            4, 0, 3,   3, 7, 4,
        };

        std::vector<typename ShaderType::VertexInput> vertices;
        vertices.reserve(indices.size() * 3);
        for (const auto i : indices)
        {
            vertices.emplace_back(points[i]);
        }
        cube.vertex_count = static_cast<uint32_t>(vertices.size());

        constexpr typename ShaderType::PerObjectBuffer ubo{
            .ObjectTransform = glm::gtc::identity<glm::mat4>(),
            .Color = {1, 0, 0, 1}
        };

        if (const auto sb = staging_buffer.suballoc(vertices.size() *
            sizeof(ShaderType::VertexInput), 64))
        {
            if (const auto dst_sb = vertex_buffer.suballoc(sb->size, 64))
            {
                std::ranges::copy(vertices, static_cast<ShaderType::VertexInput*>(sb->ptr));
                copy_buffers.emplace_back(vertex_buffer, *sb, dst_sb->offset);
                cube.vertex_buffer = *dst_sb;
            }
            // defer suballocation deletion
            delete_buffers.emplace(1, std::pair(std::ref(staging_buffer), *sb));
        }
        return cube;
    }
    void exec_copy_buffers(VkCommandBuffer cmd) noexcept
    {
        ZoneScoped;
        for (auto& [buffer, sb, dst_offset] : copy_buffers)
        {
            buffer.copy_from(cmd, staging_buffer.buffer(), sb, dst_offset);
        }
        copy_buffers.clear();
    }
};
Geometry::~Geometry()
{
    if (vkr && vertex_buffer.alloc)
        vkr->vertex_buffer.subfree(vertex_buffer);
    if (vkr && uniform_buffer.alloc)
        vkr->object_buffer.subfree(uniform_buffer);
}
}
