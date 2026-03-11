#include "Pipeline.h"
#include "VulkanContext.h"
#include "Mesh.h"

#include <spdlog/spdlog.h>

#include <fstream>
#include <stdexcept>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// SPIR-V loader
// ─────────────────────────────────────────────────────────────────────────────

VkShaderModule Pipeline::loadShader(VkDevice device, const std::filesystem::path& path)
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
// Pipeline construction
// ─────────────────────────────────────────────────────────────────────────────

Pipeline::Pipeline(const VulkanContext&          ctx,
                   VkFormat                       colorFormat,
                   VkFormat                       depthFormat,
                   const std::filesystem::path&   vertSpv,
                   const std::filesystem::path&   fragSpv)
    : m_ctx(ctx)
{
    VkShaderModule vertModule = loadShader(ctx.device(), vertSpv);
    VkShaderModule fragModule = loadShader(ctx.device(), fragSpv);

    // ── Descriptor set layout: set 0, binding 0 = combined image sampler ────────
    VkDescriptorSetLayoutBinding samplerBinding{
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT
    };
    VkDescriptorSetLayoutCreateInfo setLayoutInfo{
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &samplerBinding
    };
    if (vkCreateDescriptorSetLayout(ctx.device(), &setLayoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor set layout");

    // ── Push constants: one mat4 for MVP ──────────────────────────────────────
    VkPushConstantRange pushRange{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset     = 0,
        .size       = sizeof(float) * 16   // mat4
    };

    VkPipelineLayoutCreateInfo layoutInfo{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &m_descriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &pushRange
    };
    if (vkCreatePipelineLayout(ctx.device(), &layoutInfo, nullptr, &m_layout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create pipeline layout");

    // ── Shader stages ─────────────────────────────────────────────────────────
    VkPipelineShaderStageCreateInfo stages[2]{
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertModule,
            .pName  = "main"
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragModule,
            .pName  = "main"
        }
    };

    // ── Vertex input — matches Vertex struct in Mesh.h ────────────────────────
    VkVertexInputBindingDescription binding{
        .binding   = 0,
        .stride    = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };

    VkVertexInputAttributeDescription attrs[5]{
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,     .offset = offsetof(Vertex, position)},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32_SFLOAT,           .offset = offsetof(Vertex, uvX)     },
        {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,     .offset = offsetof(Vertex, normal)  },
        {.location = 3, .binding = 0, .format = VK_FORMAT_R32_SFLOAT,           .offset = offsetof(Vertex, uvY)     },
        {.location = 4, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT,  .offset = offsetof(Vertex, color)   },
    };

    VkPipelineVertexInputStateCreateInfo vertexInput{
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &binding,
        .vertexAttributeDescriptionCount = 5,
        .pVertexAttributeDescriptions    = attrs
    };

    // ── Fixed-function state ──────────────────────────────────────────────────
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };

    // Viewport + scissor are dynamic
    VkPipelineViewportStateCreateInfo viewportState{
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount  = 1
    };

    VkPipelineRasterizationStateCreateInfo rasterizer{
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .cullMode                = VK_CULL_MODE_BACK_BIT,
        .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable         = VK_FALSE,
        .lineWidth               = 1.0f
    };

    VkPipelineMultisampleStateCreateInfo multisample{
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };

    VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable       = VK_TRUE,
        .depthWriteEnable      = VK_TRUE,
        .depthCompareOp        = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = VK_FALSE
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable    = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    VkPipelineColorBlendStateCreateInfo colorBlend{
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable   = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments    = &colorBlendAttachment
    };

    VkDynamicState dynamicStates[2]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates    = dynamicStates
    };

    // ── Dynamic rendering — no VkRenderPass ───────────────────────────────────
    VkPipelineRenderingCreateInfo renderingInfo{
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &colorFormat,
        .depthAttachmentFormat   = depthFormat
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
        .layout              = m_layout
    };

    if (vkCreateGraphicsPipelines(ctx.device(), VK_NULL_HANDLE, 1,
                                  &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create graphics pipeline");

    // Shader modules are no longer needed after pipeline creation
    vkDestroyShaderModule(ctx.device(), vertModule, nullptr);
    vkDestroyShaderModule(ctx.device(), fragModule, nullptr);

    spdlog::info("Graphics pipeline created");
}

Pipeline::~Pipeline()
{
    vkDestroyPipeline(m_ctx.device(), m_pipeline, nullptr);
    vkDestroyPipelineLayout(m_ctx.device(), m_layout, nullptr);
    vkDestroyDescriptorSetLayout(m_ctx.device(), m_descriptorSetLayout, nullptr);
}
