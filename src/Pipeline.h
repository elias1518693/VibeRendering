#pragma once

#include <vulkan/vulkan.h>
#include <filesystem>

class VulkanContext;

// Owns a VkPipelineLayout + VkPipeline for mesh rendering.
// Shaders are loaded from SPIR-V files at construction.
class Pipeline
{
public:
    Pipeline(const VulkanContext& ctx,
             VkFormat             colorFormat,
             VkFormat             depthFormat,
             const std::filesystem::path& vertSpv,
             const std::filesystem::path& fragSpv);
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    [[nodiscard]] VkPipeline            pipeline()            const { return m_pipeline;            }
    [[nodiscard]] VkPipelineLayout      layout()              const { return m_layout;              }
    [[nodiscard]] VkDescriptorSetLayout descriptorSetLayout() const { return m_descriptorSetLayout; }

private:
    static VkShaderModule loadShader(VkDevice device, const std::filesystem::path& path);

    const VulkanContext&  m_ctx;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout      m_layout              = VK_NULL_HANDLE;
    VkPipeline            m_pipeline            = VK_NULL_HANDLE;
};
