#include "App.h"
#include "VulkanContext.h"
#include "Swapchain.h"
#include "Renderer.h"

#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <stdexcept>

App::App()
{
    initWindow();
    m_ctx       = std::make_unique<VulkanContext>(m_window);
    m_swapchain = std::make_unique<Swapchain>(*m_ctx, m_window);
    m_renderer  = std::make_unique<Renderer>(*m_ctx, *m_swapchain);
    spdlog::info("App initialized — let's vibe");
}

App::~App()
{
    // m_renderer, m_swapchain, m_ctx destroyed in reverse order via unique_ptr
    if (m_window)
        glfwDestroyWindow(m_window);
    glfwTerminate();
}

void App::initWindow()
{
    if (!glfwInit())
        throw std::runtime_error("GLFW init failed");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(k_width, k_height, k_title, nullptr, nullptr);
    if (!m_window)
        throw std::runtime_error("GLFW window creation failed");

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);

    spdlog::info("Window created: {}x{}", k_width, k_height);
}

void App::run()
{
    while (!glfwWindowShouldClose(m_window))
    {
        glfwPollEvents();
        m_renderer->drawFrame(*m_swapchain, m_window, m_framebufferResized);
        m_framebufferResized = false;
    }
    m_ctx->deviceWaitIdle();
}

void App::framebufferResizeCallback(GLFWwindow* window, int /*w*/, int /*h*/)
{
    auto* app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
    app->m_framebufferResized = true;
}
