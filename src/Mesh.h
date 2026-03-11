#pragma once

#include "VulkanContext.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <string>
#include <vector>
#include <cstdint>

// Interleaved vertex layout.
// uvX/uvY are packed alongside position/normal to avoid padding waste.
struct Vertex
{
    glm::vec3 position;
    float     uvX = 0.0f;
    glm::vec3 normal;
    float     uvY = 0.0f;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

// A single draw-ready mesh on the GPU.
struct GpuMesh
{
    AllocatedBuffer vertexBuffer;
    AllocatedBuffer indexBuffer;
    uint32_t        indexCount   = 0;
    uint32_t        vertexCount  = 0;
    uint32_t        textureIndex = UINT32_MAX;  // index into LoadedScene::textures; UINT32_MAX = white fallback
    std::string     name;
};

// Owns all GPU resources loaded from a single glTF file.
struct LoadedScene
{
    std::vector<GpuMesh>        meshes;
    std::vector<AllocatedImage> textures;  // indexed by GpuMesh::textureIndex
};
