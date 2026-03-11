#pragma once

#include <vulkan/vulkan.h>
#include <vector>

struct GLFWwindow;
class VulkanContext;

class Swapchain
{
public:
    Swapchain(const VulkanContext& ctx, GLFWwindow* window);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = delete;
    Swapchain& operator=(Swapchain&&) = delete;

    // Destroys and recreates the swapchain (e.g. on window resize).
    // Blocks until the window has a non-zero framebuffer size.
    void recreate(GLFWwindow* window);

    [[nodiscard]] VkSwapchainKHR handle()     const { return m_swapchain; }
    [[nodiscard]] VkFormat       format()     const { return m_format; }
    [[nodiscard]] VkExtent2D     extent()     const { return m_extent; }
    [[nodiscard]] uint32_t       imageCount() const { return static_cast<uint32_t>(m_images.size()); }

    [[nodiscard]] const std::vector<VkImage>&     images()     const { return m_images; }
    [[nodiscard]] const std::vector<VkImageView>& imageViews() const { return m_imageViews; }

private:
    void create(GLFWwindow* window);
    void destroy();

    const VulkanContext& m_ctx;

    VkSwapchainKHR           m_swapchain = VK_NULL_HANDLE;
    VkFormat                 m_format    = VK_FORMAT_UNDEFINED;
    VkExtent2D               m_extent    = {0, 0};
    std::vector<VkImage>     m_images;
    std::vector<VkImageView> m_imageViews;
};
