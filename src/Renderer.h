#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <vector>

class VulkanContext;
class Swapchain;
struct GLFWwindow;

inline constexpr uint32_t k_maxFramesInFlight = 2;

struct FrameData
{
    VkCommandPool   commandPool   = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence         inFlight      = VK_NULL_HANDLE;
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

    // Main entry point called from App::run() every frame.
    // framebufferResized: set true when GLFW reports a resize.
    void drawFrame(Swapchain& swapchain, GLFWwindow* window, bool framebufferResized);

private:
    void createFrameData(uint32_t swapchainImageCount);
    void destroyFrameData();

    void recordCommandBuffer(VkCommandBuffer cmd,
                             VkImage         image,
                             VkImageView     imageView,
                             VkExtent2D      extent);

    static void transitionImage(VkCommandBuffer cmd,
                                VkImage         image,
                                VkImageLayout   oldLayout,
                                VkImageLayout   newLayout);

    const VulkanContext& m_ctx;

    std::array<FrameData, k_maxFramesInFlight> m_frames{};
    uint32_t m_currentFrame = 0;

    // Both semaphore pools indexed per swapchain image.
    // imageAvailable: cycled with m_acquireSemIdx before acquire.
    // renderFinished: indexed by imageIndex — safe because you can't re-acquire
    //                 image N before it's been presented (semaphore is free by then).
    std::vector<VkSemaphore> m_imageAvailableSems;
    std::vector<VkSemaphore> m_renderFinishedSems;
    uint32_t m_acquireSemIdx = 0;
};
