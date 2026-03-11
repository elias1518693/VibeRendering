#pragma once

#include "VulkanContext.h"  // AllocatedImage
#include "Mesh.h"

#include <vulkan/vulkan.h>
#include <glm/mat4x4.hpp>
#include <array>
#include <span>
#include <vector>

class Swapchain;
struct GLFWwindow;

inline constexpr uint32_t k_maxFramesInFlight = 2;
inline constexpr VkFormat k_depthFormat       = VK_FORMAT_D32_SFLOAT;

struct FrameData
{
    VkCommandPool   commandPool   = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence         inFlight      = VK_NULL_HANDLE;
};

// Everything the renderer needs to draw a frame
struct DrawContext
{
    VkPipeline               pipeline       = VK_NULL_HANDLE;
    VkPipelineLayout         pipelineLayout = VK_NULL_HANDLE;
    std::span<const GpuMesh>         meshes;
    std::span<const VkDescriptorSet> descriptorSets; // parallel to meshes
    glm::mat4                mvp            = {};
};

class Renderer
{
public:
    Renderer(const VulkanContext& ctx, const Swapchain& swapchain);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    void drawFrame(Swapchain& swapchain, GLFWwindow* window,
                   bool framebufferResized, const DrawContext& draw);

private:
    void createFrameData(uint32_t swapchainImageCount);
    void destroyFrameData();

    void createDepthImage(VkExtent2D extent);
    void destroyDepthImage();

    void recordCommandBuffer(VkCommandBuffer    cmd,
                             VkImage            colorImage,
                             VkImageView        colorView,
                             VkExtent2D         extent,
                             const DrawContext& draw);

    static void transitionImage(VkCommandBuffer    cmd,
                                VkImage            image,
                                VkImageLayout      oldLayout,
                                VkImageLayout      newLayout,
                                VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);

    const VulkanContext& m_ctx;

    std::array<FrameData, k_maxFramesInFlight> m_frames{};
    uint32_t m_currentFrame = 0;

    std::vector<VkSemaphore> m_imageAvailableSems;
    std::vector<VkSemaphore> m_renderFinishedSems;
    uint32_t m_acquireSemIdx = 0;

    AllocatedImage m_depthImage;
};
