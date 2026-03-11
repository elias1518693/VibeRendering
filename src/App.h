#pragma once

#include "Mesh.h"

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

struct GLFWwindow;
class VulkanContext;
class Swapchain;
class Renderer;
class Pipeline;

class App
{
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    void run();

private:
    void initWindow();
    void buildDescriptors();

    static void framebufferResizeCallback(GLFWwindow* window, int w, int h);

    GLFWwindow* m_window = nullptr;
    bool m_framebufferResized = false;

    // Declared in construction order; destroyed in reverse
    std::unique_ptr<VulkanContext> m_ctx;
    std::unique_ptr<Swapchain>     m_swapchain;
    std::unique_ptr<Renderer>      m_renderer;
    std::unique_ptr<Pipeline>      m_pipeline;

    LoadedScene                  m_scene;
    AllocatedImage               m_whiteTex;
    VkDescriptorPool             m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;

    static constexpr int         k_width  = 1280;
    static constexpr int         k_height = 720;
    static constexpr const char* k_title  = "VibeRendering";
};
