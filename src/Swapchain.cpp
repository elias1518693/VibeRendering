#include "Swapchain.h"
#include "VulkanContext.h"

#include <VkBootstrap.h>
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <stdexcept>

Swapchain::Swapchain(const VulkanContext& ctx, GLFWwindow* window)
    : m_ctx(ctx)
{
    create(window);
}

Swapchain::~Swapchain()
{
    destroy();
}

void Swapchain::create(GLFWwindow* window)
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);

    vkb::SwapchainBuilder builder{
        m_ctx.physicalDevice(),
        m_ctx.device(),
        m_ctx.surface(),
        m_ctx.graphicsQueueFamily(),
        m_ctx.graphicsQueueFamily()
    };

    builder.set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
    builder.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR);
    builder.add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR);
    builder.set_desired_extent(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    builder.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    auto result = builder.build();

    if (!result)
        throw std::runtime_error("Swapchain creation failed: " + result.error().message());

    auto vkbSwap  = result.value();
    m_swapchain   = vkbSwap.swapchain;
    m_format      = vkbSwap.image_format;
    m_extent      = vkbSwap.extent;
    m_images      = vkbSwap.get_images().value();
    m_imageViews  = vkbSwap.get_image_views().value();

    spdlog::info("Swapchain created: {}x{}, {} images, format={}",
        m_extent.width, m_extent.height, m_images.size(),
        static_cast<int>(m_format));
}

void Swapchain::destroy()
{
    for (auto view : m_imageViews)
        vkDestroyImageView(m_ctx.device(), view, nullptr);
    m_imageViews.clear();
    m_images.clear();

    if (m_swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(m_ctx.device(), m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

void Swapchain::recreate(GLFWwindow* window)
{
    // Spin until the window has a non-zero framebuffer (handles minimization)
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    while (w == 0 || h == 0)
    {
        glfwGetFramebufferSize(window, &w, &h);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(m_ctx.device());
    destroy();
    create(window);

    spdlog::info("Swapchain recreated");
}
