#pragma once

#include <memory>

struct GLFWwindow;
class VulkanContext;
class Swapchain;
class Renderer;

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

    static void framebufferResizeCallback(GLFWwindow* window, int w, int h);

    GLFWwindow* m_window = nullptr;
    bool m_framebufferResized = false;

    // Declared in construction order; destroyed in reverse
    std::unique_ptr<VulkanContext> m_ctx;
    std::unique_ptr<Swapchain>     m_swapchain;
    std::unique_ptr<Renderer>      m_renderer;

    static constexpr int         k_width  = 1280;
    static constexpr int         k_height = 720;
    static constexpr const char* k_title  = "VibeRendering";
};
