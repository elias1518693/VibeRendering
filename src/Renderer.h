#pragma once

#include "VulkanContext.h"  // AllocatedImage, VmaAllocation
#include "Mesh.h"

#include <vulkan/vulkan.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <array>
#include <filesystem>
#include <span>
#include <vector>

class Swapchain;
struct GLFWwindow;

inline constexpr uint32_t k_maxFramesInFlight = 2;
inline constexpr VkFormat k_depthFormat       = VK_FORMAT_D32_SFLOAT;
inline constexpr uint32_t k_shadowCubeSize    = 512;   // per-face resolution

struct FrameData
{
    VkCommandPool   commandPool   = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence         inFlight      = VK_NULL_HANDLE;
};

// Fragment push constants pushed at offset 64 for the main pipeline
struct FragPC
{
    glm::vec4 lightPos;    // offset 64
    float     ambient;     // offset 80
    float     diffuse;     // offset 84
    float     shadowBias;  // offset 88
    float     pcfRadius;   // offset 92
    float     attLin;      // offset 96
    float     attQuad;     // offset 100
};
static_assert(sizeof(FragPC) == 40);

// Everything the renderer needs to draw a frame
struct DrawContext
{
    VkPipeline               pipeline       = VK_NULL_HANDLE;
    VkPipelineLayout         pipelineLayout = VK_NULL_HANDLE;
    std::span<const GpuMesh>         meshes;
    std::span<const VkDescriptorSet> descriptorSets; // parallel to meshes
    glm::mat4                mvp            = {};

    // Lighting (fed into FragPC push constants)
    glm::vec3 lightPos   = {0.0f, 25.0f, 0.0f};
    float     ambient    = 0.08f;
    float     diffuse    = 1.2f;
    float     shadowBias = 0.02f;
    float     pcfRadius  = 0.05f;
    float     attLin     = 0.008f;
    float     attQuad    = 0.0008f;

    // ImGui draw data — nullptr means skip ImGui pass
    struct ImDrawData* imguiDrawData = nullptr;
};

class Renderer
{
public:
    Renderer(const VulkanContext& ctx, const Swapchain& swapchain,
             const std::filesystem::path& shadowCubeVertSpv,
             const std::filesystem::path& shadowCubeFragSpv);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    void drawFrame(Swapchain& swapchain, GLFWwindow* window,
                   bool framebufferResized, const DrawContext& draw);

    // Expose the cube shadow map for descriptor set writes in App
    [[nodiscard]] VkImageView shadowCubeView() const { return m_shadowCubeView; }
    [[nodiscard]] VkSampler   shadowSampler()  const { return m_shadowSampler;  }

private:
    void createFrameData(uint32_t swapchainImageCount);
    void destroyFrameData();

    void createDepthImage(VkExtent2D extent);
    void destroyDepthImage();

    void createShadowResources(const std::filesystem::path& vertSpv,
                               const std::filesystem::path& fragSpv);
    void destroyShadowResources();

    void recordShadowPass(VkCommandBuffer cmd, const DrawContext& draw);

    void recordCommandBuffer(VkCommandBuffer    cmd,
                             VkImage            colorImage,
                             VkImageView        colorView,
                             VkExtent2D         extent,
                             const DrawContext& draw);

    static void transitionImage(VkCommandBuffer        cmd,
                                VkImage                image,
                                VkImageLayout          oldLayout,
                                VkImageLayout          newLayout,
                                VkImageSubresourceRange subresourceRange);

    static void transitionImage(VkCommandBuffer    cmd,
                                VkImage            image,
                                VkImageLayout      oldLayout,
                                VkImageLayout      newLayout,
                                VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);

    static VkShaderModule loadShaderModule(VkDevice device,
                                           const std::filesystem::path& path);

    const VulkanContext& m_ctx;

    std::array<FrameData, k_maxFramesInFlight> m_frames{};
    uint32_t m_currentFrame = 0;

    std::vector<VkSemaphore> m_imageAvailableSems;
    std::vector<VkSemaphore> m_renderFinishedSems;
    uint32_t m_acquireSemIdx = 0;

    AllocatedImage m_depthImage;

    // Shadow cubemap (R32_SFLOAT, 6 layers) — stores linear distance from light
    VkImage          m_shadowCubeImg   = VK_NULL_HANDLE;
    VmaAllocation    m_shadowCubeAlloc = nullptr;
    VkImageView      m_shadowCubeView  = VK_NULL_HANDLE;            // CUBE view for sampling
    std::array<VkImageView, 6> m_shadowFaceViews = {};               // 2D views for rendering

    // Aux depth buffer reused across all 6 shadow passes
    AllocatedImage m_shadowDepth;

    VkSampler        m_shadowSampler          = VK_NULL_HANDLE;
    VkPipelineLayout m_shadowPipelineLayout   = VK_NULL_HANDLE;
    VkPipeline       m_shadowPipeline         = VK_NULL_HANDLE;
};
