#pragma once

#include <vulkan/vulkan.h>

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

private:
    void createInstance();
    void createSurface(GLFWwindow* window);
    void selectPhysicalDeviceAndCreateDevice();
    void createAllocator();

    // vk-bootstrap wrapper structs — kept alive so PhysicalDeviceSelector
    // and destructor helpers (vkb::destroy_*) have valid handles.
    // Using void* + forward declarations to avoid including VkBootstrap.h here.
    struct VkbHandles;
    VkbHandles* m_vkb = nullptr; // heap-allocated to avoid full header include

    VkInstance       m_instance       = VK_NULL_HANDLE;
    VkSurfaceKHR     m_surface        = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkQueue          m_graphicsQueue  = VK_NULL_HANDLE;
    uint32_t         m_graphicsQueueFamily = 0;
    VmaAllocator     m_allocator      = VK_NULL_HANDLE;
};
