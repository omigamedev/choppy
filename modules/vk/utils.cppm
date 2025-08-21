module;
#include <string>
#include <type_traits>
#include <volk.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_to_string.hpp>
#include <vulkan/vulkan_core.h>

export module ce.vk.utils;
import glm;

export namespace ce::vk::utils
{
struct FrameContext final
{
    VkCommandBuffer cmd;
    VkExtent2D size;
    VkImage color_image;
    VkImage depth_image;
    VkImage resolve_color_image;
    VkImageView color_view;
    VkImageView depth_view;
    VkImageView resolve_color_view;
    VkFramebuffer framebuffer;
    VkRenderPass renderpass;
    VkFence fence;
    int64_t display_time;
    glm::mat4 view[2];
    glm::mat4 projection[2];
    glm::vec3 view_pos[2];
    glm::quat view_quat[2];
    glm::vec3 head_pos;
    glm::quat head_quat;
};
[[nodiscard]] VkFormat find_format(const VkPhysicalDevice physical_device,
const std::span<const VkFormat> formats, const VkFormatFeatureFlags features) noexcept
{
    for (const auto format : formats)
    {
        VkFormatProperties2 props{.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};
        vkGetPhysicalDeviceFormatProperties2(physical_device, format, &props);
        if (props.formatProperties.optimalTilingFeatures & features)
            return format;
    }
    return VK_FORMAT_UNDEFINED;
}
[[nodiscard]] const char* to_string(VkResult r) noexcept
{
    static char sr[64]{0};
    const std::string ss = ::vk::to_string(static_cast<::vk::Result>(r));
    std::copy_n(ss.begin(), std::min(ss.size(), sizeof(sr) - 1), sr);
    return sr;
}
template<typename T>
constexpr VkObjectType get_type(T obj) noexcept
{
    if constexpr (std::is_same_v<T, VkInstance>) return VK_OBJECT_TYPE_INSTANCE;
    else if constexpr (std::is_same_v<T, VkPhysicalDevice>) return VK_OBJECT_TYPE_PHYSICAL_DEVICE;
    else if constexpr (std::is_same_v<T, VkDevice>) return VK_OBJECT_TYPE_DEVICE;
    else if constexpr (std::is_same_v<T, VkQueue>) return VK_OBJECT_TYPE_QUEUE;
    else if constexpr (std::is_same_v<T, VkSemaphore>) return VK_OBJECT_TYPE_SEMAPHORE;
    else if constexpr (std::is_same_v<T, VkCommandBuffer>) return VK_OBJECT_TYPE_COMMAND_BUFFER;
    else if constexpr (std::is_same_v<T, VkFence>) return VK_OBJECT_TYPE_FENCE;
    else if constexpr (std::is_same_v<T, VkDeviceMemory>) return VK_OBJECT_TYPE_DEVICE_MEMORY;
    else if constexpr (std::is_same_v<T, VkBuffer>) return VK_OBJECT_TYPE_BUFFER;
    else if constexpr (std::is_same_v<T, VkImage>) return VK_OBJECT_TYPE_IMAGE;
    else if constexpr (std::is_same_v<T, VkEvent>) return VK_OBJECT_TYPE_EVENT;
    else if constexpr (std::is_same_v<T, VkQueryPool>) return VK_OBJECT_TYPE_QUERY_POOL;
    else if constexpr (std::is_same_v<T, VkBufferView>) return VK_OBJECT_TYPE_BUFFER_VIEW;
    else if constexpr (std::is_same_v<T, VkImageView>) return VK_OBJECT_TYPE_IMAGE_VIEW;
    else if constexpr (std::is_same_v<T, VkShaderModule>) return VK_OBJECT_TYPE_SHADER_MODULE;
    else if constexpr (std::is_same_v<T, VkPipelineCache>) return VK_OBJECT_TYPE_PIPELINE_CACHE;
    else if constexpr (std::is_same_v<T, VkPipelineLayout>) return VK_OBJECT_TYPE_PIPELINE_LAYOUT;
    else if constexpr (std::is_same_v<T, VkRenderPass>) return VK_OBJECT_TYPE_RENDER_PASS;
    else if constexpr (std::is_same_v<T, VkPipeline>) return VK_OBJECT_TYPE_PIPELINE;
    else if constexpr (std::is_same_v<T, VkDescriptorSetLayout>) return VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
    else if constexpr (std::is_same_v<T, VkSampler>) return VK_OBJECT_TYPE_SAMPLER;
    else if constexpr (std::is_same_v<T, VkDescriptorPool>) return VK_OBJECT_TYPE_DESCRIPTOR_POOL;
    else if constexpr (std::is_same_v<T, VkDescriptorSet>) return VK_OBJECT_TYPE_DESCRIPTOR_SET;
    else if constexpr (std::is_same_v<T, VkFramebuffer>) return VK_OBJECT_TYPE_FRAMEBUFFER;
    else if constexpr (std::is_same_v<T, VkCommandPool>) return VK_OBJECT_TYPE_COMMAND_POOL;
    else if constexpr (std::is_same_v<T, VkSamplerYcbcrConversion>) return VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION;
    else if constexpr (std::is_same_v<T, VkDescriptorUpdateTemplate>) return VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE;
    else if constexpr (std::is_same_v<T, VkPrivateDataSlot>) return VK_OBJECT_TYPE_PRIVATE_DATA_SLOT;
    else if constexpr (std::is_same_v<T, VkSurfaceKHR>) return VK_OBJECT_TYPE_SURFACE_KHR;
    else if constexpr (std::is_same_v<T, VkSwapchainKHR>) return VK_OBJECT_TYPE_SWAPCHAIN_KHR;
    else if constexpr (std::is_same_v<T, VkDisplayKHR>) return VK_OBJECT_TYPE_DISPLAY_KHR;
    else if constexpr (std::is_same_v<T, VkDisplayModeKHR>) return VK_OBJECT_TYPE_DISPLAY_MODE_KHR;
    else if constexpr (std::is_same_v<T, VkDebugReportCallbackEXT>) return VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT;
    else if constexpr (std::is_same_v<T, VkVideoSessionKHR>) return VK_OBJECT_TYPE_VIDEO_SESSION_KHR;
    else if constexpr (std::is_same_v<T, VkVideoSessionParametersKHR>) return VK_OBJECT_TYPE_VIDEO_SESSION_PARAMETERS_KHR;
    else if constexpr (std::is_same_v<T, VkCuModuleNVX>) return VK_OBJECT_TYPE_CU_MODULE_NVX;
    else if constexpr (std::is_same_v<T, VkCuFunctionNVX>) return VK_OBJECT_TYPE_CU_FUNCTION_NVX;
    else if constexpr (std::is_same_v<T, VkDebugUtilsMessengerEXT>) return VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT;
    else if constexpr (std::is_same_v<T, VkAccelerationStructureKHR>) return VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR;
    else if constexpr (std::is_same_v<T, VkValidationCacheEXT>) return VK_OBJECT_TYPE_VALIDATION_CACHE_EXT;
    else if constexpr (std::is_same_v<T, VkAccelerationStructureNV>) return VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV;
    else if constexpr (std::is_same_v<T, VkPerformanceConfigurationINTEL>) return VK_OBJECT_TYPE_PERFORMANCE_CONFIGURATION_INTEL;
    else if constexpr (std::is_same_v<T, VkDeferredOperationKHR>) return VK_OBJECT_TYPE_DEFERRED_OPERATION_KHR;
    else if constexpr (std::is_same_v<T, VkIndirectCommandsLayoutNV>) return VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_NV;
    // else if constexpr (std::is_same_v<T, VkCudaModuleNVX>) return VK_OBJECT_TYPE_CUDA_MODULE_NVX;
    // else if constexpr (std::is_same_v<T, VkCudaFunctionNVX>) return VK_OBJECT_TYPE_CUDA_FUNCTION_NVX;
    // else if constexpr (std::is_same_v<T, VkBUFFERCOLLECTIONFUCHSIA>) return VK_OBJECT_TYPE_BUFFER_COLLECTION_FUCHSIA;
    else if constexpr (std::is_same_v<T, VkMicromapEXT>) return VK_OBJECT_TYPE_MICROMAP_EXT;
    else if constexpr (std::is_same_v<T, VkOpticalFlowSessionNV>) return VK_OBJECT_TYPE_OPTICAL_FLOW_SESSION_NV;
    else if constexpr (std::is_same_v<T, VkShaderEXT>) return VK_OBJECT_TYPE_SHADER_EXT;
    else if constexpr (std::is_same_v<T, VkPipelineBinaryKHR>) return VK_OBJECT_TYPE_PIPELINE_BINARY_KHR;
    else if constexpr (std::is_same_v<T, VkIndirectCommandsLayoutEXT>) return VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_EXT;
    else if constexpr (std::is_same_v<T, VkIndirectExecutionSetEXT>) return VK_OBJECT_TYPE_INDIRECT_EXECUTION_SET_EXT;
    else if constexpr (std::is_same_v<T, VkDescriptorUpdateTemplateKHR>) return VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_KHR;
    else if constexpr (std::is_same_v<T, VkSamplerYcbcrConversionKHR>) return VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION_KHR;
    else if constexpr (std::is_same_v<T, VkPrivateDataSlotEXT>) return VK_OBJECT_TYPE_PRIVATE_DATA_SLOT_EXT;
    else static_assert(!sizeof(T), "Unsupported Vulkan handle type");
    return VK_OBJECT_TYPE_UNKNOWN;
};

}
