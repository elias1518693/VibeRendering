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
    createImmediateSubmitResources();
    createDefaultSampler();
}

VulkanContext::~VulkanContext()
{
    if (m_defaultSampler)  vkDestroySampler(m_device, m_defaultSampler, nullptr);
    if (m_immediateFence)  vkDestroyFence(m_device, m_immediateFence, nullptr);
    if (m_immediatePool)   vkDestroyCommandPool(m_device, m_immediatePool, nullptr);

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

void VulkanContext::createImmediateSubmitResources()
{
    VkCommandPoolCreateInfo poolInfo{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_graphicsQueueFamily
    };
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_immediatePool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create immediate command pool");

    VkCommandBufferAllocateInfo cbInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = m_immediatePool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    if (vkAllocateCommandBuffers(m_device, &cbInfo, &m_immediateCmd) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate immediate command buffer");

    VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    if (vkCreateFence(m_device, &fenceInfo, nullptr, &m_immediateFence) != VK_SUCCESS)
        throw std::runtime_error("Failed to create immediate fence");
}

AllocatedBuffer VulkanContext::createBuffer(VkDeviceSize          size,
                                             VkBufferUsageFlags    usage,
                                             VmaMemoryUsage        memUsage,
                                             VmaAllocationCreateFlags extraFlags) const
{
    VkBufferCreateInfo bufInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = size,
        .usage = usage
    };

    VmaAllocationCreateInfo allocInfo{
        .flags = extraFlags,
        .usage = memUsage
    };

    AllocatedBuffer result;
    if (vmaCreateBuffer(m_allocator, &bufInfo, &allocInfo,
                        &result.buffer, &result.allocation, &result.allocInfo) != VK_SUCCESS)
        throw std::runtime_error("Failed to create buffer");

    return result;
}

void VulkanContext::destroyBuffer(const AllocatedBuffer& buf) const
{
    vmaDestroyBuffer(m_allocator, buf.buffer, buf.allocation);
}

AllocatedImage VulkanContext::createImage(VkExtent2D         extent,
                                           VkFormat           format,
                                           VkImageUsageFlags  usage,
                                           VkImageAspectFlags aspectFlags) const
{
    VkImageCreateInfo imageInfo{
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = format,
        .extent        = {extent.width, extent.height, 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = usage,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VmaAllocationCreateInfo allocInfo{ .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE };

    AllocatedImage result;
    result.extent = extent;
    result.format = format;

    if (vmaCreateImage(m_allocator, &imageInfo, &allocInfo,
                       &result.image, &result.allocation, nullptr) != VK_SUCCESS)
        throw std::runtime_error("Failed to create image");

    VkImageViewCreateInfo viewInfo{
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = result.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = format,
        .subresourceRange = {
            .aspectMask     = aspectFlags,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1
        }
    };

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &result.imageView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create image view");

    return result;
}

void VulkanContext::destroyImage(const AllocatedImage& img) const
{
    vkDestroyImageView(m_device, img.imageView, nullptr);
    vmaDestroyImage(m_allocator, img.image, img.allocation);
}

void VulkanContext::immediateSubmit(std::function<void(VkCommandBuffer)>&& fn) const
{
    vkResetFences(m_device, 1, &m_immediateFence);
    vkResetCommandBuffer(m_immediateCmd, 0);

    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vkBeginCommandBuffer(m_immediateCmd, &beginInfo);
    fn(m_immediateCmd);
    vkEndCommandBuffer(m_immediateCmd);

    VkSubmitInfo submitInfo{
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &m_immediateCmd
    };
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_immediateFence);
    vkWaitForFences(m_device, 1, &m_immediateFence, VK_TRUE, UINT64_MAX);
}

AllocatedImage VulkanContext::createImageFromData(VkExtent2D   extent,
                                                   VkFormat     format,
                                                   const void*  pixels,
                                                   VkDeviceSize byteSize) const
{
    AllocatedBuffer staging = createBuffer(byteSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    vmaCopyMemoryToAllocation(m_allocator, pixels, staging.allocation, 0, byteSize);

    AllocatedImage img = createImage(extent, format,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    immediateSubmit([&](VkCommandBuffer cmd)
    {
        auto barrier = [&](VkImageLayout oldLayout, VkImageLayout newLayout,
                           VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                           VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
        {
            VkImageMemoryBarrier2 b{
                .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask     = srcStage,
                .srcAccessMask    = srcAccess,
                .dstStageMask     = dstStage,
                .dstAccessMask    = dstAccess,
                .oldLayout        = oldLayout,
                .newLayout        = newLayout,
                .image            = img.image,
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            };
            VkDependencyInfo dep{
                .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers    = &b
            };
            vkCmdPipelineBarrier2(cmd, &dep);
        };

        barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

        VkBufferImageCopy region{
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageExtent      = {extent.width, extent.height, 1}
        };
        vkCmdCopyBufferToImage(cmd, staging.buffer, img.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COPY_BIT,           VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    });

    destroyBuffer(staging);
    return img;
}

void VulkanContext::createDefaultSampler()
{
    VkSamplerCreateInfo info{
        .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter    = VK_FILTER_LINEAR,
        .minFilter    = VK_FILTER_LINEAR,
        .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .maxLod       = VK_LOD_CLAMP_NONE
    };
    if (vkCreateSampler(m_device, &info, nullptr, &m_defaultSampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create default sampler");
}
