#pragma once

#include "Mesh.h"

#include <vulkan/vulkan.h>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <memory>
#include <vector>

struct LightSettings
{
    glm::vec3 pos        = {0.0f, 25.0f, 0.0f};
    float     ambient    = 0.08f;
    float     diffuse    = 1.2f;
    float     shadowBias = 0.02f;
    float     pcfRadius  = 0.05f;
    float     attLin     = 0.008f;
    float     attQuad    = 0.0008f;
};

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
    void initImGui();
    void shutdownImGui();

    static void framebufferResizeCallback(GLFWwindow* window, int w, int h);
    static void mouseMoveCallback(GLFWwindow* window, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

    GLFWwindow* m_window           = nullptr;
    bool        m_framebufferResized = false;

    // Fly camera state
    glm::vec3 m_cameraPos    = {0.0f, 4.0f, 0.0f};
    float     m_yaw          = 0.0f;   // degrees, 0 = looking along +X
    float     m_pitch        = 0.0f;   // degrees, clamped ±89
    bool      m_mouseCapture = false;
    bool      m_firstMouse   = true;
    double    m_lastMouseX   = 0.0;
    double    m_lastMouseY   = 0.0;
    double    m_lastFrameTime = 0.0;

    // Declared in construction order; destroyed in reverse
    std::unique_ptr<VulkanContext> m_ctx;
    std::unique_ptr<Swapchain>     m_swapchain;
    std::unique_ptr<Renderer>      m_renderer;
    std::unique_ptr<Pipeline>      m_pipeline;

    LoadedScene                  m_scene;
    AllocatedImage               m_whiteTex;
    VkDescriptorPool             m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;

    LightSettings                m_light;
    VkDescriptorPool             m_imguiPool = VK_NULL_HANDLE;

    static constexpr int         k_width  = 1280;
    static constexpr int         k_height = 720;
    static constexpr const char* k_title  = "VibeRendering";
    static constexpr float       k_moveSpeed    = 8.0f;   // units per second
    static constexpr float       k_mouseSensitivity = 0.1f; // degrees per pixel
};
