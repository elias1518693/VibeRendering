#include "GltfLoader.h"
#include "VulkanContext.h"

#pragma warning(push, 0)
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#pragma warning(pop)

#define STB_IMAGE_IMPLEMENTATION
#pragma warning(push, 0)
#include <stb_image.h>
#pragma warning(pop)

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <spdlog/spdlog.h>

#include <stdexcept>
#include <string>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

static GpuMesh uploadMesh(const std::string&         name,
                          const std::vector<Vertex>& vertices,
                          const std::vector<uint32_t>& indices,
                          const VulkanContext&         ctx)
{
    const VkDeviceSize vertexBytes = vertices.size() * sizeof(Vertex);
    const VkDeviceSize indexBytes  = indices.size()  * sizeof(uint32_t);

    AllocatedBuffer vertexStaging = ctx.createBuffer(
        vertexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    AllocatedBuffer indexStaging = ctx.createBuffer(
        indexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

    vmaCopyMemoryToAllocation(ctx.allocator(), vertices.data(), vertexStaging.allocation, 0, vertexBytes);
    vmaCopyMemoryToAllocation(ctx.allocator(), indices.data(),  indexStaging.allocation,  0, indexBytes);

    GpuMesh mesh;
    mesh.name        = name;
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.indexCount  = static_cast<uint32_t>(indices.size());

    mesh.vertexBuffer = ctx.createBuffer(vertexBytes,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    mesh.indexBuffer = ctx.createBuffer(indexBytes,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    ctx.immediateSubmit([&](VkCommandBuffer cmd)
    {
        VkBufferCopy vertCopy{.size = vertexBytes};
        vkCmdCopyBuffer(cmd, vertexStaging.buffer, mesh.vertexBuffer.buffer, 1, &vertCopy);
        VkBufferCopy idxCopy{.size = indexBytes};
        vkCmdCopyBuffer(cmd, indexStaging.buffer, mesh.indexBuffer.buffer, 1, &idxCopy);
    });

    ctx.destroyBuffer(vertexStaging);
    ctx.destroyBuffer(indexStaging);
    return mesh;
}

// Upload a single glTF image by index. Returns a 1×1 white image on failure.
static AllocatedImage uploadGltfImage(const fastgltf::Asset&         asset,
                                       std::size_t                    imageIndex,
                                       const std::filesystem::path&   gltfDir,
                                       const VulkanContext&            ctx)
{
    const auto& gltfImage = asset.images[imageIndex];

    int w = 0, h = 0, ch = 0;
    stbi_uc* pixels = nullptr;

    std::visit(fastgltf::visitor{
        [&](const fastgltf::sources::URI& uri) {
            auto fullPath = gltfDir / std::string(uri.uri.path());
            pixels = stbi_load(fullPath.string().c_str(), &w, &h, &ch, STBI_rgb_alpha);
        },
        [&](const fastgltf::sources::Array& arr) {
            pixels = stbi_load_from_memory(
                reinterpret_cast<const stbi_uc*>(arr.bytes.data()),
                static_cast<int>(arr.bytes.size()), &w, &h, &ch, STBI_rgb_alpha);
        },
        [&](const fastgltf::sources::BufferView& bv) {
            auto& view   = asset.bufferViews[bv.bufferViewIndex];
            auto& buffer = asset.buffers[view.bufferIndex];
            std::visit(fastgltf::visitor{
                [&](const fastgltf::sources::Array& arr) {
                    pixels = stbi_load_from_memory(
                        reinterpret_cast<const stbi_uc*>(arr.bytes.data()) + view.byteOffset,
                        static_cast<int>(view.byteLength), &w, &h, &ch, STBI_rgb_alpha);
                },
                [](auto&) {}
            }, buffer.data);
        },
        [](auto&) {}
    }, gltfImage.data);

    if (!pixels || w == 0 || h == 0)
    {
        spdlog::warn("  Failed to decode image [{}], using white fallback", imageIndex);
        constexpr uint32_t white = 0xFFFFFFFF;
        return ctx.createImageFromData({1, 1}, VK_FORMAT_R8G8B8A8_SRGB, &white, 4);
    }

    auto img = ctx.createImageFromData(
        {static_cast<uint32_t>(w), static_cast<uint32_t>(h)},
        VK_FORMAT_R8G8B8A8_SRGB, pixels,
        static_cast<VkDeviceSize>(w * h * 4));
    stbi_image_free(pixels);
    return img;
}

// ─────────────────────────────────────────────────────────────────────────────
// GltfLoader::load
// ─────────────────────────────────────────────────────────────────────────────

LoadedScene GltfLoader::load(const std::filesystem::path& path,
                              const VulkanContext&          ctx)
{
    spdlog::info("Loading glTF: {}", path.string());

    fastgltf::Parser parser;

    constexpr auto gltfOptions =
        fastgltf::Options::LoadExternalBuffers |
        fastgltf::Options::GenerateMeshIndices;

    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None)
        throw std::runtime_error("Failed to read glTF file: " + path.string());

    auto assetResult = parser.loadGltf(data.get(), path.parent_path(), gltfOptions);
    if (assetResult.error() != fastgltf::Error::None)
        throw std::runtime_error("Failed to parse glTF: "
            + std::string(fastgltf::getErrorMessage(assetResult.error())));

    fastgltf::Asset& asset = assetResult.get();

    LoadedScene scene;
    scene.meshes.reserve(asset.meshes.size());

    // Cache: glTF imageIndex → index in scene.textures (deduplicates shared textures)
    std::unordered_map<std::size_t, uint32_t> imageCache;

    for (auto& mesh : asset.meshes)
    {
        for (auto& primitive : mesh.primitives)
        {
            if (primitive.type != fastgltf::PrimitiveType::Triangles)
                continue;

            std::vector<Vertex>   vertices;
            std::vector<uint32_t> indices;

            // ── Indices ──────────────────────────────────────────────────────
            if (primitive.indicesAccessor.has_value())
            {
                auto& accessor = asset.accessors[primitive.indicesAccessor.value()];
                indices.resize(accessor.count);
                fastgltf::iterateAccessorWithIndex<uint32_t>(asset, accessor,
                    [&](uint32_t idx, std::size_t i) { indices[i] = idx; });
            }

            // ── Positions ────────────────────────────────────────────────────
            auto* posAttr = primitive.findAttribute("POSITION");
            if (posAttr == primitive.attributes.end()) continue;

            {
                auto& accessor = asset.accessors[posAttr->accessorIndex];
                vertices.resize(accessor.count);
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, accessor,
                    [&](fastgltf::math::fvec3 pos, std::size_t i) {
                        vertices[i].position = {pos.x(), pos.y(), pos.z()};
                    });
            }

            // ── Normals ──────────────────────────────────────────────────────
            if (auto* attr = primitive.findAttribute("NORMAL"); attr != primitive.attributes.end())
            {
                auto& accessor = asset.accessors[attr->accessorIndex];
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, accessor,
                    [&](fastgltf::math::fvec3 n, std::size_t i) {
                        vertices[i].normal = {n.x(), n.y(), n.z()};
                    });
            }

            // ── UVs ──────────────────────────────────────────────────────────
            if (auto* attr = primitive.findAttribute("TEXCOORD_0"); attr != primitive.attributes.end())
            {
                auto& accessor = asset.accessors[attr->accessorIndex];
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(asset, accessor,
                    [&](fastgltf::math::fvec2 uv, std::size_t i) {
                        vertices[i].uvX = uv.x();
                        vertices[i].uvY = uv.y();
                    });
            }

            // ── Vertex colors ────────────────────────────────────────────────
            if (auto* attr = primitive.findAttribute("COLOR_0"); attr != primitive.attributes.end())
            {
                auto& accessor = asset.accessors[attr->accessorIndex];
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(asset, accessor,
                    [&](fastgltf::math::fvec4 c, std::size_t i) {
                        vertices[i].color = {c.x(), c.y(), c.z(), c.w()};
                    });
            }

            // ── Base-color texture ────────────────────────────────────────────
            uint32_t texIdx = UINT32_MAX;
            if (primitive.materialIndex.has_value())
            {
                auto& mat = asset.materials[primitive.materialIndex.value()];
                if (mat.pbrData.baseColorTexture.has_value())
                {
                    auto& texInfo = mat.pbrData.baseColorTexture.value();
                    auto& tex     = asset.textures[texInfo.textureIndex];
                    if (tex.imageIndex.has_value())
                    {
                        std::size_t imgIdx = tex.imageIndex.value();
                        auto [it, inserted] = imageCache.emplace(imgIdx,
                            static_cast<uint32_t>(scene.textures.size()));
                        if (inserted)
                        {
                            spdlog::debug("  Uploading texture image [{}]", imgIdx);
                            scene.textures.push_back(
                                uploadGltfImage(asset, imgIdx, path.parent_path(), ctx));
                        }
                        texIdx = it->second;
                    }
                }
            }

            spdlog::debug("  Mesh '{}': {} verts, {} indices, texIdx={}",
                mesh.name, vertices.size(), indices.size(), texIdx);

            GpuMesh gpuMesh = uploadMesh(std::string(mesh.name), vertices, indices, ctx);
            gpuMesh.textureIndex = texIdx;
            scene.meshes.push_back(std::move(gpuMesh));
        }
    }

    spdlog::info("Loaded {} primitives, {} unique textures from '{}'",
        scene.meshes.size(), scene.textures.size(), path.filename().string());
    return scene;
}
