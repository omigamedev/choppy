module;
#include <string>
#include <utility>
#include <vector>
#include <array>
#include <format>
#include <functional>
#include <string_view>
#include <span>
#include <ranges>

#include <volk.h>
#include <vk_mem_alloc.h>

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#else
#define LOGE(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

export module ce.vk.buffer;
import ce.vk;
import ce.vk.utils;

export namespace ce::vk
{
struct BufferSuballocation
{
    BufferSuballocation() = default;
    BufferSuballocation(const VmaVirtualAllocation alloc, const VkDeviceSize offset, const VkDeviceSize size,
        void* const ptr)
        : alloc(alloc),
          offset(offset),
          size(size),
          ptr(ptr)
    {
    }
    BufferSuballocation(const BufferSuballocation& other) = default;
    BufferSuballocation(BufferSuballocation&& other) noexcept
        : alloc(other.alloc),
          offset(other.offset),
          size(other.size),
          ptr(other.ptr)
    {
    }
    BufferSuballocation& operator=(const BufferSuballocation& other) = default;
    BufferSuballocation& operator=(BufferSuballocation&& other) noexcept
    {
        if (this == &other)
            return *this;
        alloc = other.alloc;
        offset = other.offset;
        size = other.size;
        ptr = other.ptr;
        other.alloc = VK_NULL_HANDLE;
        other.offset = 0;
        other.size = 0;
        other.ptr = nullptr;
        return *this;
    }

    VmaVirtualAllocation alloc = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    void* ptr = nullptr;
};

class Buffer
{
public:
    Buffer(const Buffer& other) = delete;
    Buffer(Buffer&& other) noexcept = default;
    Buffer& operator=(const Buffer& other) = delete;
    Buffer& operator=(Buffer&& other) noexcept = default;

private:
    std::weak_ptr<Context> m_vk;
    std::string m_name;
    VkDeviceSize m_size = 0;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo m_allocation_info{};
    VkDeviceSize m_staging_size = 0;
    VkBuffer m_staging_buffer = VK_NULL_HANDLE;
    VmaAllocation m_staging_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo m_staging_allocation_info{};
    VmaVirtualBlock m_virtual_block = VK_NULL_HANDLE;

public:
    Buffer() = default;
    Buffer(const std::shared_ptr<Context>& vk, std::string name) noexcept
        : m_vk(vk), m_name(std::move(name)) { }
    ~Buffer() noexcept
    {
        if (m_staging_buffer)
            destroy_staging();
        if (m_buffer)
            destroy();
    }
    [[nodiscard]] VkBuffer buffer() const noexcept { return m_buffer; }
    [[nodiscard]] VkDeviceSize size() const noexcept { return m_size; }
    bool create(const VkDeviceSize size, const VkBufferUsageFlags usage, const VmaMemoryUsage memory_usage,
        const VmaAllocationCreateFlags flags = {0}) noexcept
    {
        const auto vk = m_vk.lock();
        const VkBufferCreateInfo buffer_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = usage,
        };
        const VmaAllocationCreateInfo alloc_info{
            .flags = flags,
            .usage = memory_usage,
        };
        if (const VkResult result = vmaCreateBuffer(vk->vma(), &buffer_info, &alloc_info,
            &m_buffer, &m_allocation, &m_allocation_info); result != VK_SUCCESS)
        {
            LOGE("Failed to create buffer");
            return false;
        }
        vk->debug_name(m_name, m_buffer);
        m_size = size;
        return true;
    }
    [[nodiscard]] std::optional<BufferSuballocation> suballoc(const VkDeviceSize size,
        const VkDeviceSize alignment) noexcept
    {
        if (!m_virtual_block)
        {
            const VmaVirtualBlockCreateInfo block_info{.size = m_size};
            if (const VkResult result = vmaCreateVirtualBlock(&block_info, &m_virtual_block);
                result != VK_SUCCESS)
            {
                LOGE("Failed to create virtual block");
                return std::nullopt;
            }
        }
        const VmaVirtualAllocationCreateInfo alloc_create_info = {.size = size, .alignment = alignment};
        VmaVirtualAllocation alloc;
        VkDeviceSize offset;
        if (const VkResult result = vmaVirtualAllocate(m_virtual_block, &alloc_create_info, &alloc, &offset);
            result != VK_SUCCESS)
        {
            LOGE("Failed to allocate virtual block");
            return std::nullopt;
        }
        TracyAllocN(static_cast<uint8_t*>(m_allocation_info.pMappedData) + offset, size, m_name.c_str());
        return BufferSuballocation{alloc, offset, size, static_cast<uint8_t*>(m_allocation_info.pMappedData) + offset};
    }
    void subfree(const BufferSuballocation& suballoc) noexcept
    {
        if (m_virtual_block)
        {
            TracyFreeN(suballoc.ptr, m_name.c_str());
            vmaVirtualFree(m_virtual_block, suballoc.alloc);
        }
    }
    bool create_staging(const VkDeviceSize staging_size) noexcept
    {
        if (staging_size == 0)
        {
            LOGE("Failed to create staging buffer: staging_size = 0");
            return false;
        }
        const auto vk = m_vk.lock();
        const VkBufferCreateInfo buffer_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = m_size,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        };
        constexpr VmaAllocationCreateInfo alloc_info{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
        };
        if (const VkResult result = vmaCreateBuffer(vk->vma(), &buffer_info, &alloc_info,
            &m_staging_buffer, &m_staging_allocation, &m_staging_allocation_info); result != VK_SUCCESS)
        {
            LOGE("Failed to create staging buffer: %s", utils::to_string(result));
            return false;
        }
        vk->debug_name(std::format("{}_staging", m_name), m_staging_buffer);
        m_staging_size = staging_size;
        return true;
    }
    template<typename T = void*>
    [[nodiscard]] T* staging_ptr() noexcept
    {
        return static_cast<T*>(m_staging_allocation_info.pMappedData);
    }
    void destroy() noexcept
    {
        if (!m_buffer)
        {
            LOGE("Failed to destroy buffer: already destroyed or never created");
            return;
        }
        if (m_virtual_block)
        {
            vmaDestroyVirtualBlock(m_virtual_block);
        }
        if (const auto vk = m_vk.lock())
        {
            vmaDestroyBuffer(vk->vma(), m_buffer, m_allocation);
            m_buffer = VK_NULL_HANDLE;
            m_allocation = VK_NULL_HANDLE;
            m_allocation_info = {};
            m_size = 0;
        }
    }
    void destroy_staging() noexcept
    {
        if (!m_staging_buffer)
        {
            LOGE("Failed to destroy staging buffer: already destroyed");
            return;
        }
        if (const auto vk = m_vk.lock())
        {
            vmaDestroyBuffer(vk->vma(), m_staging_buffer, m_staging_allocation);
            m_staging_buffer = VK_NULL_HANDLE;
            m_staging_allocation = VK_NULL_HANDLE;
            m_staging_allocation_info = {};
            m_staging_size = 0;
        }
    }
    void copy_from(VkCommandBuffer cmd, VkBuffer src_buffer, const BufferSuballocation& src, VkDeviceSize dst_offset) const noexcept
    {
        const VkBufferCopy copy_info{src.offset, dst_offset, src.size};
        vkCmdCopyBuffer(cmd, src_buffer, m_buffer, 1, &copy_info);
    }
    bool update_cmd(VkCommandBuffer cmd, const std::span<const uint8_t> data, VkDeviceSize src_offset, VkDeviceSize offset) const noexcept
    {
        if (!m_staging_buffer)
        {
            LOGE("Failed to update NULL staging buffer");
            return false;
        }
        if (!cmd)
        {
            LOGE("Failed to update staging buffer: NULL command buffer");
            return false;
        }
        if (!m_buffer)
        {
            LOGE("Failed to update NULL buffer");
            return false;
        }
        if (data.size_bytes() > m_staging_size)
        {
            LOGE("Failed to update staging buffer: data bigger than staging size");
            return false;
        }
        if (data.size_bytes() + offset > m_size)
        {
            LOGE("Failed to update buffer: data+offset out of bounds");
            return false;
        }
        std::ranges::copy(data, static_cast<uint8_t*>(m_staging_allocation_info.pMappedData) + src_offset);
        const VkBufferCopy copy_info{src_offset, offset, data.size_bytes()};
        vkCmdCopyBuffer(cmd, m_staging_buffer, m_buffer, 1, &copy_info);
        return true;
    }
    template<typename T>
    bool update_cmd(VkCommandBuffer cmd, const std::span<T> data, VkDeviceSize offset = 0) const noexcept
    {
        return update_cmd(cmd, {reinterpret_cast<const uint8_t*>(data.data()), data.size_bytes()}, 0, offset);
    }
    template<typename T>
    bool update_cmd(VkCommandBuffer cmd, const std::span<const T> data) const noexcept
    {
        return update_cmd(cmd, {reinterpret_cast<const uint8_t*>(data.data()), data.size_bytes()}, 0, 0);
    }
    template<typename T>
    bool update_cmd(VkCommandBuffer cmd, const T& data) const noexcept
    {
        return update_cmd(cmd, std::bit_cast<const std::array<const uint8_t, sizeof(T)>>(data), 0, 0);
    }
};
}
