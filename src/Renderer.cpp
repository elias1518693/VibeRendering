#include "Renderer.h"
#include "VulkanContext.h"
#include "Swapchain.h"

#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <array>
#include <stdexcept>

Renderer::Renderer(const VulkanContext& ctx, const Swapchain& swapchain)
    : m_ctx(ctx)
{
    createFrameData(swapchain.imageCount());
    spdlog::info("Renderer initialized ({} frames in flight, {} acquire semaphores)",
        k_maxFramesInFlight, m_imageAvailableSems.size());
}

Renderer::~Renderer()
{
    destroyFrameData();
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame data: per-frame command pool, command buffer, semaphores, fence
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::createFrameData(uint32_t swapchainImageCount)
{
    VkCommandPoolCreateInfo poolInfo{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_ctx.graphicsQueueFamily()
    };

    VkCommandBufferAllocateInfo cbAllocInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    VkSemaphoreCreateInfo semInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    for (auto& frame : m_frames)
    {
        if (vkCreateCommandPool(m_ctx.device(), &poolInfo, nullptr, &frame.commandPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create command pool");

        cbAllocInfo.commandPool = frame.commandPool;
        if (vkAllocateCommandBuffers(m_ctx.device(), &cbAllocInfo, &frame.commandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate command buffer");

        if (vkCreateFence(m_ctx.device(), &fenceInfo, nullptr, &frame.inFlight) != VK_SUCCESS)
            throw std::runtime_error("Failed to create fence");
    }

    // One semaphore per swapchain image for both acquire and render-finished.
    // This guarantees neither semaphore is reused while the WSI still holds it.
    m_imageAvailableSems.resize(swapchainImageCount);
    m_renderFinishedSems.resize(swapchainImageCount);
    for (uint32_t i = 0; i < swapchainImageCount; ++i)
    {
        if (vkCreateSemaphore(m_ctx.device(), &semInfo, nullptr, &m_imageAvailableSems[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_ctx.device(), &semInfo, nullptr, &m_renderFinishedSems[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create semaphores");
    }
    m_acquireSemIdx = 0;
}

void Renderer::destroyFrameData()
{
    for (auto sem : m_imageAvailableSems)
        vkDestroySemaphore(m_ctx.device(), sem, nullptr);
    for (auto sem : m_renderFinishedSems)
        vkDestroySemaphore(m_ctx.device(), sem, nullptr);
    m_imageAvailableSems.clear();
    m_renderFinishedSems.clear();

    for (auto& frame : m_frames)
    {
        vkDestroyFence(m_ctx.device(), frame.inFlight, nullptr);
        vkDestroyCommandPool(m_ctx.device(), frame.commandPool, nullptr);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Image layout transition using Vulkan 1.3 Synchronization2
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::transitionImage(VkCommandBuffer cmd,
                               VkImage         image,
                               VkImageLayout   oldLayout,
                               VkImageLayout   newLayout)
{
    VkImageMemoryBarrier2 barrier{
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask     = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask    = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask     = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask    = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .oldLayout        = oldLayout,
        .newLayout        = newLayout,
        .image            = image,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1
        }
    };

    VkDependencyInfo depInfo{
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrier
    };

    vkCmdPipelineBarrier2(cmd, &depInfo);
}

// ─────────────────────────────────────────────────────────────────────────────
// Record a command buffer: transition → dynamic rendering clear → transition
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::recordCommandBuffer(VkCommandBuffer cmd,
                                   VkImage         image,
                                   VkImageView     imageView,
                                   VkExtent2D      extent)
{
    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vkBeginCommandBuffer(cmd, &beginInfo);

    // 1. UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
    transitionImage(cmd, image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // 2. Dynamic rendering — no VkRenderPass needed!
    VkClearColorValue clearColor{.float32 = {0.08f, 0.04f, 0.18f, 1.0f}}; // deep vibe purple

    VkRenderingAttachmentInfo colorAttachment{
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = imageView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = {.color = clearColor}
    };

    VkRenderingInfo renderingInfo{
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea           = {.offset = {0, 0}, .extent = extent},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &colorAttachment
    };

    vkCmdBeginRendering(cmd, &renderingInfo);
    // Future: bind pipeline and issue draw calls here
    vkCmdEndRendering(cmd);

    // 3. COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR
    transitionImage(cmd, image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    vkEndCommandBuffer(cmd);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawFrame: acquire → record → submit → present
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::drawFrame(Swapchain& swapchain, GLFWwindow* window, bool framebufferResized)
{
    FrameData& frame = m_frames[m_currentFrame];

    // Wait for this frame slot's previous GPU work to finish
    vkWaitForFences(m_ctx.device(), 1, &frame.inFlight, VK_TRUE, UINT64_MAX);

    // Pick the next imageAvailable semaphore from the rotating pool.
    // Cycling through imageCount semaphores ensures we never reuse one still held by the WSI.
    VkSemaphore acquireSem = m_imageAvailableSems[m_acquireSemIdx];
    m_acquireSemIdx = (m_acquireSemIdx + 1) % static_cast<uint32_t>(m_imageAvailableSems.size());

    // Acquire next swapchain image
    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        m_ctx.device(), swapchain.handle(), UINT64_MAX,
        acquireSem, VK_NULL_HANDLE, &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        swapchain.recreate(window);
        return;
        // NOTE: fence NOT reset here — still signaled, correct because
        //       we never submitted work to it this frame.
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("Failed to acquire swapchain image");

    // Only reset the fence once we know we'll submit work
    vkResetFences(m_ctx.device(), 1, &frame.inFlight);

    // Record
    vkResetCommandPool(m_ctx.device(), frame.commandPool, 0);
    recordCommandBuffer(frame.commandBuffer,
        swapchain.images()[imageIndex],
        swapchain.imageViews()[imageIndex],
        swapchain.extent());

    // Submit: wait on the semaphore used for this acquire, signal the one for this image slot.
    // Both indexed by imageIndex so they're never reused while the WSI holds them.
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &acquireSem,
        .pWaitDstStageMask    = &waitStage,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &frame.commandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &m_renderFinishedSems[imageIndex]
    };
    vkQueueSubmit(m_ctx.graphicsQueue(), 1, &submitInfo, frame.inFlight);

    // Present
    VkSwapchainKHR swapchainHandle = swapchain.handle();
    VkPresentInfoKHR presentInfo{
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &m_renderFinishedSems[imageIndex],
        .swapchainCount     = 1,
        .pSwapchains        = &swapchainHandle,
        .pImageIndices      = &imageIndex
    };
    VkResult presentResult = vkQueuePresentKHR(m_ctx.graphicsQueue(), &presentInfo);

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR         ||
        framebufferResized)
    {
        swapchain.recreate(window);
    }
    else if (presentResult != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to present swapchain image");
    }

    m_currentFrame = (m_currentFrame + 1) % k_maxFramesInFlight;
}
