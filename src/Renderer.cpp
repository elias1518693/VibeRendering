#include "Renderer.h"
#include "VulkanContext.h"
#include "Swapchain.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>

#include <array>
#include <stdexcept>

Renderer::Renderer(const VulkanContext& ctx, const Swapchain& swapchain)
    : m_ctx(ctx)
{
    createFrameData(swapchain.imageCount());
    createDepthImage(swapchain.extent());
    spdlog::info("Renderer initialized ({} frames in flight)", k_maxFramesInFlight);
}

Renderer::~Renderer()
{
    destroyDepthImage();
    destroyFrameData();
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame data
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
// Depth image
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::createDepthImage(VkExtent2D extent)
{
    m_depthImage = m_ctx.createImage(extent, k_depthFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void Renderer::destroyDepthImage()
{
    if (m_depthImage.image != VK_NULL_HANDLE)
    {
        m_ctx.destroyImage(m_depthImage);
        m_depthImage = {};
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Image layout transition (Sync2)
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::transitionImage(VkCommandBuffer    cmd,
                               VkImage            image,
                               VkImageLayout      oldLayout,
                               VkImageLayout      newLayout,
                               VkImageAspectFlags aspectMask)
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
            .aspectMask     = aspectMask,
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
// Command buffer recording
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::recordCommandBuffer(VkCommandBuffer    cmd,
                                   VkImage            colorImage,
                                   VkImageView        colorView,
                                   VkExtent2D         extent,
                                   const DrawContext& draw)
{
    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Transitions
    transitionImage(cmd, colorImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    transitionImage(cmd, m_depthImage.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    // Attachments
    VkRenderingAttachmentInfo colorAtt{
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = colorView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = {.color = {.float32 = {0.08f, 0.04f, 0.18f, 1.0f}}}
    };
    VkRenderingAttachmentInfo depthAtt{
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = m_depthImage.imageView,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue  = {.depthStencil = {.depth = 1.0f}}
    };

    VkRenderingInfo renderingInfo{
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea           = {.offset = {0, 0}, .extent = extent},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &colorAtt,
        .pDepthAttachment     = &depthAtt
    };

    vkCmdBeginRendering(cmd, &renderingInfo);

    if (draw.pipeline != VK_NULL_HANDLE && !draw.meshes.empty())
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.pipeline);

        VkViewport viewport{
            .x = 0.0f, .y = 0.0f,
            .width    = static_cast<float>(extent.width),
            .height   = static_cast<float>(extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{{0, 0}, extent};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdPushConstants(cmd, draw.pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4),
            glm::value_ptr(draw.mvp));

        for (uint32_t i = 0; i < static_cast<uint32_t>(draw.meshes.size()); ++i)
        {
            const auto& mesh = draw.meshes[i];

            if (!draw.descriptorSets.empty())
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    draw.pipelineLayout, 0, 1, &draw.descriptorSets[i], 0, nullptr);

            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer.buffer, &offset);
            vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
        }
    }

    vkCmdEndRendering(cmd);

    transitionImage(cmd, colorImage,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    vkEndCommandBuffer(cmd);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawFrame
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::drawFrame(Swapchain& swapchain, GLFWwindow* window,
                         bool framebufferResized, const DrawContext& draw)
{
    FrameData& frame = m_frames[m_currentFrame];
    vkWaitForFences(m_ctx.device(), 1, &frame.inFlight, VK_TRUE, UINT64_MAX);

    VkSemaphore acquireSem = m_imageAvailableSems[m_acquireSemIdx];
    m_acquireSemIdx = (m_acquireSemIdx + 1) % static_cast<uint32_t>(m_imageAvailableSems.size());

    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        m_ctx.device(), swapchain.handle(), UINT64_MAX,
        acquireSem, VK_NULL_HANDLE, &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        swapchain.recreate(window);
        destroyDepthImage();
        createDepthImage(swapchain.extent());
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("Failed to acquire swapchain image");

    vkResetFences(m_ctx.device(), 1, &frame.inFlight);
    vkResetCommandPool(m_ctx.device(), frame.commandPool, 0);

    recordCommandBuffer(frame.commandBuffer,
        swapchain.images()[imageIndex],
        swapchain.imageViews()[imageIndex],
        swapchain.extent(), draw);

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
        destroyDepthImage();
        createDepthImage(swapchain.extent());
    }
    else if (presentResult != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to present swapchain image");
    }

    m_currentFrame = (m_currentFrame + 1) % k_maxFramesInFlight;
}
