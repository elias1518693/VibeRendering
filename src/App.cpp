#include "App.h"
#include "VulkanContext.h"
#include "Swapchain.h"
#include "Renderer.h"
#include "Pipeline.h"
#include "GltfLoader.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#include <stdexcept>

App::App()
{
    initWindow();
    m_ctx       = std::make_unique<VulkanContext>(m_window);
    m_swapchain = std::make_unique<Swapchain>(*m_ctx, m_window);
    m_renderer  = std::make_unique<Renderer>(*m_ctx, *m_swapchain);

    m_pipeline = std::make_unique<Pipeline>(
        *m_ctx,
        m_swapchain->format(),
        k_depthFormat,
        "assets/shaders/mesh.vert.spv",
        "assets/shaders/mesh.frag.spv"
    );

    m_scene = GltfLoader::load("assets/Sponza.gltf", *m_ctx);

    // 1×1 white fallback for meshes with no texture
    constexpr uint32_t white = 0xFFFFFFFF;
    m_whiteTex = m_ctx->createImageFromData({1, 1}, VK_FORMAT_R8G8B8A8_SRGB, &white, 4);

    buildDescriptors();

    spdlog::info("App initialized — let's vibe");
}

App::~App()
{
    m_ctx->deviceWaitIdle();

    if (m_descriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(m_ctx->device(), m_descriptorPool, nullptr);

    m_ctx->destroyImage(m_whiteTex);

    for (auto& tex : m_scene.textures)
        m_ctx->destroyImage(tex);

    for (auto& mesh : m_scene.meshes)
    {
        m_ctx->destroyBuffer(mesh.vertexBuffer);
        m_ctx->destroyBuffer(mesh.indexBuffer);
    }

    m_pipeline.reset();
    m_renderer.reset();
    m_swapchain.reset();
    m_ctx.reset();

    if (m_window)
        glfwDestroyWindow(m_window);
    glfwTerminate();
}

void App::buildDescriptors()
{
    const uint32_t meshCount = static_cast<uint32_t>(m_scene.meshes.size());
    if (meshCount == 0) return;

    VkDescriptorPoolSize poolSize{
        .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = meshCount
    };
    VkDescriptorPoolCreateInfo poolInfo{
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = meshCount,
        .poolSizeCount = 1,
        .pPoolSizes    = &poolSize
    };
    if (vkCreateDescriptorPool(m_ctx->device(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor pool");

    std::vector<VkDescriptorSetLayout> layouts(meshCount, m_pipeline->descriptorSetLayout());
    VkDescriptorSetAllocateInfo allocInfo{
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = m_descriptorPool,
        .descriptorSetCount = meshCount,
        .pSetLayouts        = layouts.data()
    };
    m_descriptorSets.resize(meshCount);
    if (vkAllocateDescriptorSets(m_ctx->device(), &allocInfo, m_descriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate descriptor sets");

    for (uint32_t i = 0; i < meshCount; ++i)
    {
        const AllocatedImage& tex = (m_scene.meshes[i].textureIndex < m_scene.textures.size())
            ? m_scene.textures[m_scene.meshes[i].textureIndex]
            : m_whiteTex;

        VkDescriptorImageInfo imgInfo{
            .sampler     = m_ctx->defaultSampler(),
            .imageView   = tex.imageView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
        VkWriteDescriptorSet write{
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = m_descriptorSets[i],
            .dstBinding      = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo      = &imgInfo
        };
        vkUpdateDescriptorSets(m_ctx->device(), 1, &write, 0, nullptr);
    }
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

        float time = static_cast<float>(glfwGetTime());

        VkExtent2D extent = m_swapchain->extent();
        float aspect = (extent.height > 0)
            ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
            : 1.0f;

        float radius = 16.0f;
        glm::vec3 eye{
            radius * std::cos(time * 0.25f),
            7.0f,
            radius * std::sin(time * 0.25f)
        };
        glm::mat4 view = glm::lookAt(eye, glm::vec3{0.0f, 4.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
        glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 2000.0f);
        proj[1][1] *= -1.0f; // Vulkan Y flip

        DrawContext draw{
            .pipeline       = m_pipeline->pipeline(),
            .pipelineLayout = m_pipeline->layout(),
            .meshes         = m_scene.meshes,
            .descriptorSets = m_descriptorSets,
            .mvp            = proj * view
        };

        m_renderer->drawFrame(*m_swapchain, m_window, m_framebufferResized, draw);
        m_framebufferResized = false;
    }
    m_ctx->deviceWaitIdle();
}

void App::framebufferResizeCallback(GLFWwindow* window, int /*w*/, int /*h*/)
{
    auto* app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
    app->m_framebufferResized = true;
}
