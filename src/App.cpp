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
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <stdexcept>

App::App()
{
    initWindow();
    m_ctx       = std::make_unique<VulkanContext>(m_window);
    m_swapchain = std::make_unique<Swapchain>(*m_ctx, m_window);
    m_renderer  = std::make_unique<Renderer>(*m_ctx, *m_swapchain,
                                             "assets/shaders/shadow_cube.vert.spv",
                                             "assets/shaders/shadow_cube.frag.spv");

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
    initImGui();

    spdlog::info("App initialized — let's vibe");
}

App::~App()
{
    m_ctx->deviceWaitIdle();

    shutdownImGui();

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

    // 2 bindings per set: albedo (0) + shadow map (1)
    VkDescriptorPoolSize poolSize{
        .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = meshCount * 2
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
        VkDescriptorImageInfo shadowInfo{
            .sampler     = m_renderer->shadowSampler(),
            .imageView   = m_renderer->shadowCubeView(),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL  // R32_SFLOAT color image
        };
        VkWriteDescriptorSet writes[2]{
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = m_descriptorSets[i],
                .dstBinding      = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &imgInfo
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = m_descriptorSets[i],
                .dstBinding      = 1,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &shadowInfo
            }
        };
        vkUpdateDescriptorSets(m_ctx->device(), 2, writes, 0, nullptr);
    }
}

void App::initImGui()
{
    // Descriptor pool just for ImGui (needs FREE_DESCRIPTOR_SET_BIT)
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8};
    VkDescriptorPoolCreateInfo poolInfo{
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets       = 8,
        .poolSizeCount = 1,
        .pPoolSizes    = &poolSize
    };
    if (vkCreateDescriptorPool(m_ctx->device(), &poolInfo, nullptr, &m_imguiPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ImGui descriptor pool");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(m_window, /*install_callbacks=*/true);

    VkFormat colorFormat = m_swapchain->format();
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion      = VK_API_VERSION_1_3;
    initInfo.Instance        = m_ctx->instance();
    initInfo.PhysicalDevice  = m_ctx->physicalDevice();
    initInfo.Device          = m_ctx->device();
    initInfo.QueueFamily     = m_ctx->graphicsQueueFamily();
    initInfo.Queue           = m_ctx->graphicsQueue();
    initInfo.DescriptorPool  = m_imguiPool;
    initInfo.MinImageCount   = 2;
    initInfo.ImageCount      = static_cast<uint32_t>(m_swapchain->imageCount());
    initInfo.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering                             = true;
    initInfo.PipelineRenderingCreateInfo.sType               = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;

    ImGui_ImplVulkan_Init(&initInfo);
    // Fonts are uploaded automatically on the first ImGui_ImplVulkan_NewFrame() call (imgui 1.89.7+)

    spdlog::info("ImGui initialized");
}

void App::shutdownImGui()
{
    if (m_imguiPool == VK_NULL_HANDLE) return;
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(m_ctx->device(), m_imguiPool, nullptr);
    m_imguiPool = VK_NULL_HANDLE;
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
    glfwSetCursorPosCallback(m_window, mouseMoveCallback);
    glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
    glfwSetKeyCallback(m_window, keyCallback);

    spdlog::info("Window created: {}x{}", k_width, k_height);
    spdlog::info("Left-click to capture mouse, ESC to release");
}

void App::run()
{
    m_lastFrameTime = glfwGetTime();

    while (!glfwWindowShouldClose(m_window))
    {
        glfwPollEvents();

        double now = glfwGetTime();
        float  dt  = static_cast<float>(now - m_lastFrameTime);
        m_lastFrameTime = now;

        // Build forward and right vectors from yaw/pitch
        glm::vec3 front{
            std::cos(glm::radians(m_pitch)) * std::cos(glm::radians(m_yaw)),
            std::sin(glm::radians(m_pitch)),
            std::cos(glm::radians(m_pitch)) * std::sin(glm::radians(m_yaw))
        };
        front = glm::normalize(front);
        glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3{0.0f, 1.0f, 0.0f}));

        // WASD + Space/Ctrl movement (only when mouse is captured)
        if (m_mouseCapture)
        {
            float speed = k_moveSpeed;
            if (glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) speed *= 3.0f;

            if (glfwGetKey(m_window, GLFW_KEY_W)             == GLFW_PRESS) m_cameraPos += front * speed * dt;
            if (glfwGetKey(m_window, GLFW_KEY_S)             == GLFW_PRESS) m_cameraPos -= front * speed * dt;
            if (glfwGetKey(m_window, GLFW_KEY_A)             == GLFW_PRESS) m_cameraPos -= right * speed * dt;
            if (glfwGetKey(m_window, GLFW_KEY_D)             == GLFW_PRESS) m_cameraPos += right * speed * dt;
            if (glfwGetKey(m_window, GLFW_KEY_SPACE)         == GLFW_PRESS) m_cameraPos.y += speed * dt;
            if (glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL)  == GLFW_PRESS) m_cameraPos.y -= speed * dt;
        }

        // ── ImGui frame ───────────────────────────────────────────────────────
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Light Settings");
        ImGui::SeparatorText("Position");
        ImGui::DragFloat3("Pos", &m_light.pos.x, 0.5f);
        ImGui::SeparatorText("Shading");
        ImGui::SliderFloat("Ambient",    &m_light.ambient,    0.0f,  1.0f);
        ImGui::SliderFloat("Diffuse",    &m_light.diffuse,    0.0f,  5.0f);
        ImGui::SeparatorText("Attenuation");
        ImGui::DragFloat("Att Lin",  &m_light.attLin,  0.0001f, 0.0f, 0.5f, "%.5f");
        ImGui::DragFloat("Att Quad", &m_light.attQuad, 0.00001f, 0.0f, 0.05f, "%.6f");
        ImGui::SeparatorText("Shadows");
        ImGui::SliderFloat("Bias",       &m_light.shadowBias, 0.0f,  0.1f, "%.4f");
        ImGui::SliderFloat("PCF Radius", &m_light.pcfRadius,  0.0f,  0.2f, "%.4f");
        ImGui::End();

        ImGui::Render();

        VkExtent2D extent = m_swapchain->extent();
        float aspect = (extent.height > 0)
            ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
            : 1.0f;

        glm::mat4 view = glm::lookAt(m_cameraPos, m_cameraPos + front, glm::vec3{0.0f, 1.0f, 0.0f});
        glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 2000.0f);
        proj[1][1] *= -1.0f; // Vulkan Y flip

        DrawContext draw{
            .pipeline       = m_pipeline->pipeline(),
            .pipelineLayout = m_pipeline->layout(),
            .meshes         = m_scene.meshes,
            .descriptorSets = m_descriptorSets,
            .mvp            = proj * view,
            .lightPos       = m_light.pos,
            .ambient        = m_light.ambient,
            .diffuse        = m_light.diffuse,
            .shadowBias     = m_light.shadowBias,
            .pcfRadius      = m_light.pcfRadius,
            .attLin         = m_light.attLin,
            .attQuad        = m_light.attQuad,
            .imguiDrawData  = ImGui::GetDrawData(),
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

void App::mouseMoveCallback(GLFWwindow* window, double xpos, double ypos)
{
    auto* app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
    if (!app->m_mouseCapture) return;

    if (app->m_firstMouse)
    {
        app->m_lastMouseX = xpos;
        app->m_lastMouseY = ypos;
        app->m_firstMouse = false;
        return;
    }

    float dx =  static_cast<float>(xpos - app->m_lastMouseX);
    float dy = -static_cast<float>(ypos - app->m_lastMouseY); // invert Y: up = positive pitch
    app->m_lastMouseX = xpos;
    app->m_lastMouseY = ypos;

    app->m_yaw   += dx * k_mouseSensitivity;
    app->m_pitch += dy * k_mouseSensitivity;
    app->m_pitch  = glm::clamp(app->m_pitch, -89.0f, 89.0f);
}

void App::mouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/)
{
    auto* app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && !app->m_mouseCapture)
    {
        app->m_mouseCapture = true;
        app->m_firstMouse   = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
}

void App::keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    auto* app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        if (app->m_mouseCapture)
        {
            app->m_mouseCapture = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        else
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }
}
