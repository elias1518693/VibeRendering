#include "Renderer.h"
#include "VulkanContext.h"
#include "Swapchain.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// SPIR-V loader
// ─────────────────────────────────────────────────────────────────────────────

VkShaderModule Renderer::loadShaderModule(VkDevice device, const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("Failed to open shader: " + path.string());

    const size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> code(fileSize);
    file.seekg(0);
    file.read(code.data(), static_cast<std::streamsize>(fileSize));

    VkShaderModuleCreateInfo createInfo{
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = fileSize,
        .pCode    = reinterpret_cast<const uint32_t*>(code.data())
    };
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shader module: " + path.string());
    return module;
}

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────────────────────────────────────

Renderer::Renderer(const VulkanContext& ctx, const Swapchain& swapchain,
                   const std::filesystem::path& shadowCubeVertSpv,
                   const std::filesystem::path& shadowCubeFragSpv)
    : m_ctx(ctx)
{
    createFrameData(swapchain.imageCount());
    createDepthImage(swapchain.extent());
    createShadowResources(shadowCubeVertSpv, shadowCubeFragSpv);
    spdlog::info("Renderer initialized ({} frames in flight)", k_maxFramesInFlight);
}

Renderer::~Renderer()
{
    destroyShadowResources();
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
// Depth image (main pass)
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
// Shadow resources — omnidirectional cubemap
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::createShadowResources(const std::filesystem::path& vertSpv,
                                     const std::filesystem::path& fragSpv)
{
    // 1. Cubemap color image (R32_SFLOAT, 6 array layers)
    VkImageCreateInfo cubeInfo{
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = VK_FORMAT_R32_SFLOAT,
        .extent        = {k_shadowCubeSize, k_shadowCubeSize, 1},
        .mipLevels     = 1,
        .arrayLayers   = 6,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    VmaAllocationCreateInfo allocInfo{ .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE };
    if (vmaCreateImage(m_ctx.allocator(), &cubeInfo, &allocInfo,
                       &m_shadowCubeImg, &m_shadowCubeAlloc, nullptr) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow cubemap image");

    // 2. Cube image view for sampling (viewType = CUBE)
    VkImageViewCreateInfo cubeViewInfo{
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = m_shadowCubeImg,
        .viewType         = VK_IMAGE_VIEW_TYPE_CUBE,
        .format           = VK_FORMAT_R32_SFLOAT,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6}
    };
    if (vkCreateImageView(m_ctx.device(), &cubeViewInfo, nullptr, &m_shadowCubeView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow cube image view");

    // 3. One 2D image view per face for rendering
    for (uint32_t face = 0; face < 6; ++face)
    {
        VkImageViewCreateInfo faceViewInfo{
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image            = m_shadowCubeImg,
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = VK_FORMAT_R32_SFLOAT,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, face, 1}
        };
        if (vkCreateImageView(m_ctx.device(), &faceViewInfo, nullptr, &m_shadowFaceViews[face]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create shadow face image view");
    }

    // 4. Aux depth buffer (reused across all 6 passes)
    m_shadowDepth = m_ctx.createImage(
        {k_shadowCubeSize, k_shadowCubeSize},
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    // 5. Shadow sampler (linear, no compare — we compare manually in GLSL)
    VkSamplerCreateInfo samplerInfo{
        .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter    = VK_FILTER_LINEAR,
        .minFilter    = VK_FILTER_LINEAR,
        .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
    };
    if (vkCreateSampler(m_ctx.device(), &samplerInfo, nullptr, &m_shadowSampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow sampler");

    // 6. Shadow pipeline layout
    //    VERTEX  [0..63]  — mat4 lightMVP (face transform)
    //    FRAGMENT[64..79] — vec4 lightPos
    VkPushConstantRange shadowRanges[2]{
        {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,   .offset = 0,  .size = 64},
        {.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 64, .size = 16}
    };
    VkPipelineLayoutCreateInfo layoutInfo{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 2,
        .pPushConstantRanges    = shadowRanges
    };
    if (vkCreatePipelineLayout(m_ctx.device(), &layoutInfo, nullptr, &m_shadowPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow pipeline layout");

    // 7. Shadow pipeline
    VkShaderModule shadowVert = loadShaderModule(m_ctx.device(), vertSpv);
    VkShaderModule shadowFrag = loadShaderModule(m_ctx.device(), fragSpv);

    VkPipelineShaderStageCreateInfo stages[2]{
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = shadowVert,
            .pName  = "main"
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = shadowFrag,
            .pName  = "main"
        }
    };

    VkVertexInputBindingDescription binding{
        .binding   = 0,
        .stride    = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
    VkVertexInputAttributeDescription attrs[5]{
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,    .offset = offsetof(Vertex, position)},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32_SFLOAT,          .offset = offsetof(Vertex, uvX)     },
        {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,    .offset = offsetof(Vertex, normal)  },
        {.location = 3, .binding = 0, .format = VK_FORMAT_R32_SFLOAT,          .offset = offsetof(Vertex, uvY)     },
        {.location = 4, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Vertex, color)   },
    };
    VkPipelineVertexInputStateCreateInfo vertexInput{
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &binding,
        .vertexAttributeDescriptionCount = 5,
        .pVertexAttributeDescriptions    = attrs
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };
    VkPipelineViewportStateCreateInfo viewportState{
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount  = 1
    };
    VkPipelineRasterizationStateCreateInfo rasterizer{
        .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode    = VK_CULL_MODE_BACK_BIT,
        .frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth   = 1.0f
    };
    VkPipelineMultisampleStateCreateInfo multisample{
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };
    VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable  = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL
    };
    VkPipelineColorBlendAttachmentState colorBlendAtt{
        .blendEnable    = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT
    };
    VkPipelineColorBlendStateCreateInfo colorBlend{
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &colorBlendAtt
    };
    VkDynamicState dynamicStates[2]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates    = dynamicStates
    };

    const VkFormat colorFmt = VK_FORMAT_R32_SFLOAT;
    VkPipelineRenderingCreateInfo renderingInfo{
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &colorFmt,
        .depthAttachmentFormat   = VK_FORMAT_D32_SFLOAT
    };

    VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext               = &renderingInfo,
        .stageCount          = 2,
        .pStages             = stages,
        .pVertexInputState   = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState      = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState   = &multisample,
        .pDepthStencilState  = &depthStencil,
        .pColorBlendState    = &colorBlend,
        .pDynamicState       = &dynamicState,
        .layout              = m_shadowPipelineLayout
    };
    if (vkCreateGraphicsPipelines(m_ctx.device(), VK_NULL_HANDLE, 1,
                                  &pipelineInfo, nullptr, &m_shadowPipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow cube pipeline");

    vkDestroyShaderModule(m_ctx.device(), shadowVert, nullptr);
    vkDestroyShaderModule(m_ctx.device(), shadowFrag, nullptr);
    spdlog::info("Omnidirectional shadow pipeline created ({}x{} per face)", k_shadowCubeSize, k_shadowCubeSize);
}

void Renderer::destroyShadowResources()
{
    if (m_shadowPipeline       != VK_NULL_HANDLE) vkDestroyPipeline(m_ctx.device(), m_shadowPipeline, nullptr);
    if (m_shadowPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_ctx.device(), m_shadowPipelineLayout, nullptr);
    if (m_shadowSampler        != VK_NULL_HANDLE) vkDestroySampler(m_ctx.device(), m_shadowSampler, nullptr);

    if (m_shadowDepth.image != VK_NULL_HANDLE)
        m_ctx.destroyImage(m_shadowDepth);

    for (auto& v : m_shadowFaceViews)
        if (v != VK_NULL_HANDLE) { vkDestroyImageView(m_ctx.device(), v, nullptr); v = VK_NULL_HANDLE; }
    if (m_shadowCubeView  != VK_NULL_HANDLE) vkDestroyImageView(m_ctx.device(), m_shadowCubeView, nullptr);
    if (m_shadowCubeImg   != VK_NULL_HANDLE) vmaDestroyImage(m_ctx.allocator(), m_shadowCubeImg, m_shadowCubeAlloc);

    m_shadowPipeline       = VK_NULL_HANDLE;
    m_shadowPipelineLayout = VK_NULL_HANDLE;
    m_shadowSampler        = VK_NULL_HANDLE;
    m_shadowDepth          = {};
    m_shadowCubeView       = VK_NULL_HANDLE;
    m_shadowCubeImg        = VK_NULL_HANDLE;
    m_shadowCubeAlloc      = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Image layout transitions (Sync2)
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::transitionImage(VkCommandBuffer cmd, VkImage image,
                               VkImageLayout oldLayout, VkImageLayout newLayout,
                               VkImageSubresourceRange subresourceRange)
{
    VkImageMemoryBarrier2 barrier{
        .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .oldLayout     = oldLayout,
        .newLayout     = newLayout,
        .image         = image,
        .subresourceRange = subresourceRange
    };
    VkDependencyInfo dep{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                          .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier };
    vkCmdPipelineBarrier2(cmd, &dep);
}

void Renderer::transitionImage(VkCommandBuffer cmd, VkImage image,
                               VkImageLayout oldLayout, VkImageLayout newLayout,
                               VkImageAspectFlags aspectMask)
{
    transitionImage(cmd, image, oldLayout, newLayout,
        {aspectMask, 0, 1, 0, 1});
}

// ─────────────────────────────────────────────────────────────────────────────
// Omnidirectional shadow pass (6 faces)
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::recordShadowPass(VkCommandBuffer cmd, const DrawContext& draw)
{
    // Face directions (OpenGL/Vulkan cubemap convention) and up vectors
    static const glm::vec3 k_faceTargets[6] = {
        { 1,  0,  0}, {-1,  0,  0},
        { 0,  1,  0}, { 0, -1,  0},
        { 0,  0,  1}, { 0,  0, -1}
    };
    static const glm::vec3 k_faceUps[6] = {
        {0, -1,  0}, {0, -1,  0},
        {0,  0,  1}, {0,  0, -1},
        {0, -1,  0}, {0, -1,  0}
    };

    // Transition all 6 faces: UNDEFINED → COLOR_ATTACHMENT
    transitionImage(cmd, m_shadowCubeImg,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6});

    // Transition aux depth: UNDEFINED → DEPTH_ATTACHMENT (reused for all 6 passes)
    transitionImage(cmd, m_shadowDepth.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    // 90° perspective, no Y-flip — cubemap UVs are consistent with Vulkan's framebuffer
    const glm::mat4 lightProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 1000.0f);
    const glm::vec4 lightPosVec4{draw.lightPos, 1.0f};

    VkViewport viewport{.x=0,.y=0,
        .width  = static_cast<float>(k_shadowCubeSize),
        .height = static_cast<float>(k_shadowCubeSize),
        .minDepth = 0.0f, .maxDepth = 1.0f};
    VkRect2D scissor{{0, 0}, {k_shadowCubeSize, k_shadowCubeSize}};

    for (uint32_t face = 0; face < 6; ++face)
    {
        glm::mat4 faceMVP = lightProj *
            glm::lookAt(draw.lightPos,
                        draw.lightPos + k_faceTargets[face],
                        k_faceUps[face]);

        VkRenderingAttachmentInfo colorAtt{
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView   = m_shadowFaceViews[face],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue  = {.color = {.float32 = {1.0f, 0.0f, 0.0f, 0.0f}}}  // max distance
        };
        VkRenderingAttachmentInfo depthAtt{
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView   = m_shadowDepth.imageView,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue  = {.depthStencil = {.depth = 1.0f}}
        };
        VkRenderingInfo renderingInfo{
            .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea           = {.offset = {0, 0}, .extent = {k_shadowCubeSize, k_shadowCubeSize}},
            .layerCount           = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &colorAtt,
            .pDepthAttachment     = &depthAtt
        };

        vkCmdBeginRendering(cmd, &renderingInfo);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Push face MVP to vertex stage (offset 0)
        vkCmdPushConstants(cmd, m_shadowPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4),
            glm::value_ptr(faceMVP));
        // Push light position to fragment stage (offset 64)
        vkCmdPushConstants(cmd, m_shadowPipelineLayout,
            VK_SHADER_STAGE_FRAGMENT_BIT, 64, sizeof(glm::vec4),
            &lightPosVec4);

        for (const auto& mesh : draw.meshes)
        {
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer.buffer, &offset);
            vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
        }

        vkCmdEndRendering(cmd);
    }

    // Transition all 6 faces: COLOR_ATTACHMENT → SHADER_READ_ONLY
    transitionImage(cmd, m_shadowCubeImg,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6});
}

// ─────────────────────────────────────────────────────────────────────────────
// Main command buffer
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

    // ── Omnidirectional shadow pass (6 faces) ─────────────────────────────────
    recordShadowPass(cmd, draw);

    // ── Main color pass ───────────────────────────────────────────────────────
    transitionImage(cmd, colorImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    transitionImage(cmd, m_depthImage.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    VkRenderingAttachmentInfo colorAtt{
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = colorView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}}
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

        VkViewport viewport{.x=0,.y=0,
            .width    = static_cast<float>(extent.width),
            .height   = static_cast<float>(extent.height),
            .minDepth = 0.0f, .maxDepth = 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{{0, 0}, extent};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdPushConstants(cmd, draw.pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4),
            glm::value_ptr(draw.mvp));

        // Push lighting params to fragment stage
        FragPC fpc{
            .lightPos   = glm::vec4{draw.lightPos, 1.0f},
            .ambient    = draw.ambient,
            .diffuse    = draw.diffuse,
            .shadowBias = draw.shadowBias,
            .pcfRadius  = draw.pcfRadius,
            .attLin     = draw.attLin,
            .attQuad    = draw.attQuad,
        };
        vkCmdPushConstants(cmd, draw.pipelineLayout,
            VK_SHADER_STAGE_FRAGMENT_BIT, 64, sizeof(FragPC), &fpc);

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

    // ── ImGui pass (LOAD_OP_LOAD — composites on top of rendered scene) ───────
    if (draw.imguiDrawData != nullptr)
    {
        VkRenderingAttachmentInfo imguiColorAtt{
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView   = colorView,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        };
        VkRenderingInfo imguiRenderingInfo{
            .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea           = {.offset = {0, 0}, .extent = extent},
            .layerCount           = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &imguiColorAtt,
        };
        vkCmdBeginRendering(cmd, &imguiRenderingInfo);
        ImGui_ImplVulkan_RenderDrawData(draw.imguiDrawData, cmd);
        vkCmdEndRendering(cmd);
    }

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
        throw std::runtime_error("Failed to present swapchain image");

    m_currentFrame = (m_currentFrame + 1) % k_maxFramesInFlight;
}
