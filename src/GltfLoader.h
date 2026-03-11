#pragma once

#include "Mesh.h"

#include <filesystem>

class VulkanContext;

class GltfLoader
{
public:
    // Load all meshes and textures from a glTF / glB file.
    // Caller is responsible for destroying GPU resources via ctx.destroy*() when done.
    [[nodiscard]] static LoadedScene load(const std::filesystem::path& path,
                                          const VulkanContext&          ctx);
};
