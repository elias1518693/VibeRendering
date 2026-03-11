// VMA implementation — exactly ONE translation unit defines this
#pragma warning(push, 0)
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#pragma warning(pop)

#include "VulkanContext.h"

#include <VkBootstrap.h>
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <stdexcept>
#include <string>

// Concrete definition of the opaque vkb handle struct
struct VulkanContext::VkbHandles
{
    vkb::Instance instance;
    vkb::Device   device;
};

VulkanContext::VulkanContext(GLFWwindow* window)
    : m_vkb(new VkbHandles{})
{
    createInstance();
    createSurface(window);
    selectPhysicalDeviceAndCreateDevice();
    createAllocator();
}

VulkanContext::~VulkanContext()
{
    if (m_allocator)
        vmaDestroyAllocator(m_allocator);

    if (m_vkb)
    {
        vkb::destroy_device(m_vkb->device);
        if (m_surface != VK_NULL_HANDLE)
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        vkb::destroy_instance(m_vkb->instance);
        delete m_vkb;
    }
}

void VulkanContext::createInstance()
{
    vkb::InstanceBuilder builder;
    builder.set_app_name("VibeRendering")
           .set_engine_name("VibeEngine")
           .require_api_version(1, 3, 0);

#ifdef VIBE_VALIDATION_LAYERS
    builder.request_validation_layers(true)
           .use_default_debug_messenger();
    spdlog::info("Validation layers: enabled");
#else
    spdlog::info("Validation layers: disabled (Release build)");
#endif

    auto result = builder.build();
    if (!result)
        throw std::runtime_error("Vulkan instance creation failed: " + result.error().message());

    m_vkb->instance = result.value();
    m_instance      = m_vkb->instance.instance;

    spdlog::info("Vulkan instance created (API 1.3)");
}

void VulkanContext::createSurface(GLFWwindow* window)
{
    VkResult res = glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface);
    if (res != VK_SUCCESS)
        throw std::runtime_error("Failed to create window surface");

    spdlog::debug("Window surface created");
}

void VulkanContext::selectPhysicalDeviceAndCreateDevice()
{
    // Require Vulkan 1.3 features: dynamic rendering + synchronization2
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    // Also want Vulkan 1.2 features: buffer device address, descriptor indexing
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType                                        = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress                          = VK_TRUE;
    features12.descriptorIndexing                           = VK_TRUE;

    vkb::PhysicalDeviceSelector selector{m_vkb->instance};
    auto physResult = selector
        .set_surface(m_surface)
        .set_minimum_version(1, 3)
        .set_required_features_13(features13)
        .set_required_features_12(features12)
        .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
        .select();

    if (!physResult)
        throw std::runtime_error("No suitable GPU found: " + physResult.error().message());

    auto& physDev = physResult.value();
    spdlog::info("GPU selected: {}", std::string(physDev.properties.deviceName));

    vkb::DeviceBuilder deviceBuilder{physDev};
    auto devResult = deviceBuilder.build();
    if (!devResult)
        throw std::runtime_error("Logical device creation failed: " + devResult.error().message());

    m_vkb->device    = devResult.value();
    m_physicalDevice = m_vkb->device.physical_device;
    m_device         = m_vkb->device.device;

    auto queueResult = m_vkb->device.get_queue(vkb::QueueType::graphics);
    if (!queueResult)
        throw std::runtime_error("Failed to get graphics queue: " + queueResult.error().message());

    m_graphicsQueue       = queueResult.value();
    m_graphicsQueueFamily = m_vkb->device.get_queue_index(vkb::QueueType::graphics).value();

    spdlog::info("Logical device created, graphics queue family: {}", m_graphicsQueueFamily);
}

void VulkanContext::createAllocator()
{
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice   = m_physicalDevice;
    allocatorInfo.device           = m_device;
    allocatorInfo.instance         = m_instance;
    allocatorInfo.vulkanApiVersion  = VK_API_VERSION_1_3;
    allocatorInfo.flags            = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    VkResult res = vmaCreateAllocator(&allocatorInfo, &m_allocator);
    if (res != VK_SUCCESS)
        throw std::runtime_error("VMA allocator creation failed");

    spdlog::debug("VMA allocator created");
}

void VulkanContext::deviceWaitIdle() const
{
    vkDeviceWaitIdle(m_device);
}
