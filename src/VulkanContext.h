#pragma once

#include <vulkan/vulkan.h>
#include <functional>

// Forward-declare vk-bootstrap types to avoid polluting all headers
namespace vkb
{
struct Instance;
struct Device;
} // namespace vkb

// Suppress VMA warnings under /W4
#pragma warning(push, 0)
#include <vk_mem_alloc.h>
#pragma warning(pop)

struct GLFWwindow;

// A GPU buffer + its VMA allocation, returned by createBuffer().
struct AllocatedBuffer
{
    VkBuffer          buffer     = VK_NULL_HANDLE;
    VmaAllocation     allocation = nullptr;
    VmaAllocationInfo allocInfo  = {};
};

// A GPU image + view + VMA allocation, returned by createImage().
struct AllocatedImage
{
    VkImage       image      = VK_NULL_HANDLE;
    VkImageView   imageView  = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkExtent2D    extent     = {};
    VkFormat      format     = VK_FORMAT_UNDEFINED;
};

// Owns: VkInstance, VkPhysicalDevice, VkDevice, VkQueue, VkSurfaceKHR, VmaAllocator
// Created via vk-bootstrap. Destroyed in ~VulkanContext().
class VulkanContext
{
public:
    explicit VulkanContext(GLFWwindow* window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    [[nodiscard]] VkInstance       instance()       const { return m_instance; }
    [[nodiscard]] VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    [[nodiscard]] VkDevice         device()         const { return m_device; }
    [[nodiscard]] VkQueue          graphicsQueue()  const { return m_graphicsQueue; }
    [[nodiscard]] uint32_t         graphicsQueueFamily() const { return m_graphicsQueueFamily; }
    [[nodiscard]] VkSurfaceKHR     surface()        const { return m_surface; }
    [[nodiscard]] VmaAllocator     allocator()      const { return m_allocator; }

    void deviceWaitIdle() const;

    // ── Buffer helpers ────────────────────────────────────────────────────────
    // Create a GPU buffer.  Use VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE for GPU-only
    // buffers and VMA_MEMORY_USAGE_AUTO for staging buffers (combined with
    // VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT).
    [[nodiscard]] AllocatedBuffer createBuffer(VkDeviceSize          size,
                                               VkBufferUsageFlags    usage,
                                               VmaMemoryUsage        memUsage,
                                               VmaAllocationCreateFlags extraFlags = 0) const;
    void destroyBuffer(const AllocatedBuffer& buf) const;

    // Create a GPU image with an image view.
    // aspectFlags: VK_IMAGE_ASPECT_COLOR_BIT or VK_IMAGE_ASPECT_DEPTH_BIT.
    [[nodiscard]] AllocatedImage createImage(VkExtent2D         extent,
                                              VkFormat           format,
                                              VkImageUsageFlags  usage,
                                              VkImageAspectFlags aspectFlags) const;
    // Upload pixel data via a staging buffer; result is in SHADER_READ_ONLY_OPTIMAL layout.
    [[nodiscard]] AllocatedImage createImageFromData(VkExtent2D  extent,
                                                      VkFormat    format,
                                                      const void* pixels,
                                                      VkDeviceSize byteSize) const;
    void destroyImage(const AllocatedImage& img) const;

    // A shared linear-repeat sampler suitable for most texture lookups.
    [[nodiscard]] VkSampler defaultSampler() const { return m_defaultSampler; }

    // ── Immediate submit ──────────────────────────────────────────────────────
    // Records and submits a one-shot command buffer synchronously.
    // Useful for staging uploads, mipmap generation, etc.
    void immediateSubmit(std::function<void(VkCommandBuffer)>&& fn) const;

private:
    void createInstance();
    void createSurface(GLFWwindow* window);
    void selectPhysicalDeviceAndCreateDevice();
    void createAllocator();
    void createImmediateSubmitResources();
    void createDefaultSampler();

    // vk-bootstrap wrapper structs — kept alive so PhysicalDeviceSelector
    // and destructor helpers (vkb::destroy_*) have valid handles.
    struct VkbHandles;
    VkbHandles* m_vkb = nullptr;

    VkInstance       m_instance       = VK_NULL_HANDLE;
    VkSurfaceKHR     m_surface        = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkQueue          m_graphicsQueue  = VK_NULL_HANDLE;
    uint32_t         m_graphicsQueueFamily = 0;
    VmaAllocator     m_allocator      = VK_NULL_HANDLE;

    // Immediate submit resources
    VkCommandPool   m_immediatePool  = VK_NULL_HANDLE;
    VkCommandBuffer m_immediateCmd   = VK_NULL_HANDLE;
    VkFence         m_immediateFence = VK_NULL_HANDLE;

    VkSampler m_defaultSampler = VK_NULL_HANDLE;
};
