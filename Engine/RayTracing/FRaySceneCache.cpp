#include "FRaySceneCache.h"

#include "BVH.h"
#include "FRenderScene.h"
#include "FRenderMath.h"
#include "FTextureSamplingPolicy.h"
#include "UMesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <unordered_set>

namespace
{
constexpr int TexelsPerTriangle = 7;
constexpr std::uint64_t ExactFloatIntegerLimit = 16777216ull;
constexpr std::size_t MaxPackedMaterialAtlasBytes = 256u * 1024u * 1024u;

void HashBytes(std::uint64_t& hash, const void* bytes, std::size_t count)
{
    const auto* data = static_cast<const unsigned char*>(bytes);
    for (std::size_t i = 0; i < count; ++i)
    {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
}

struct FTlasBox
{
    glm::vec3 minimum;
    glm::vec3 maximum;
    glm::vec3 center;
    int instance = 0;
};

int BuildTlasNode(std::vector<FTlasBox>& boxes, int start, int count,
                  std::vector<BVHNode>& nodes, std::vector<std::uint32_t>& order)
{
    const int nodeIndex = static_cast<int>(nodes.size());
    nodes.push_back({});
    glm::vec3 minimum(1.0e30f), maximum(-1.0e30f);
    for (int i = start; i < start + count; ++i)
    {
        minimum = glm::min(minimum, boxes[i].minimum);
        maximum = glm::max(maximum, boxes[i].maximum);
    }
    BVHNode node;
    node.bbMin = minimum;
    node.bbMax = maximum;
    if (count <= 2)
    {
        node.leftOrTriStart = static_cast<int>(order.size());
        node.rightOrTriCount = count;
        for (int i = start; i < start + count; ++i)
            order.push_back(static_cast<std::uint32_t>(boxes[i].instance));
        nodes[static_cast<std::size_t>(nodeIndex)] = node;
        return nodeIndex;
    }
    const glm::vec3 extent = maximum - minimum;
    const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 :
        (extent.y >= extent.z ? 1 : 2);
    std::stable_sort(boxes.begin() + start, boxes.begin() + start + count,
        [axis](const FTlasBox& a, const FTlasBox& b)
        {
            if (a.center[axis] != b.center[axis]) return a.center[axis] < b.center[axis];
            return a.instance < b.instance;
        });
    const int half = count / 2;
    node.leftOrTriStart = BuildTlasNode(boxes, start, half, nodes, order);
    node.rightOrTriCount = -BuildTlasNode(
        boxes, start + half, count - half, nodes, order);
    nodes[static_cast<std::size_t>(nodeIndex)] = node;
    return nodeIndex;
}

int NodeDepth(const std::vector<BVHNode>& nodes, int index)
{
    if (index < 0 || index >= static_cast<int>(nodes.size())) return 0;
    const BVHNode& node = nodes[static_cast<std::size_t>(index)];
    if (node.rightOrTriCount > 0) return 1;
    return 1 + std::max(NodeDepth(nodes, node.leftOrTriStart),
                        NodeDepth(nodes, -node.rightOrTriCount));
}

float EncodeUInt(std::uint32_t value)
{
    float encoded = 0.0f;
    static_assert(sizeof(encoded) == sizeof(value), "uint bits must fit a float texel");
    std::memcpy(&encoded, &value, sizeof(value));
    return encoded;
}

const FResolvedRenderMaterial& MaterialAt(const FRenderMeshInstance& instance,
                                          std::size_t index)
{
    static const FResolvedRenderMaterial fallback;
    if (instance.materialOverride) return *instance.materialOverride;
    if (instance.materialSlots.empty()) return fallback;
    return instance.materialSlots[index < instance.materialSlots.size() ? index : 0u];
}

std::size_t MaterialCount(const FRenderMeshInstance& instance)
{
    return instance.materialOverride ? 1u : std::max<std::size_t>(1u,
        instance.materialSlots.size());
}

std::uint32_t MaterialIdentityAt(const FRenderMeshInstance& instance,
                                 std::size_t index)
{
    if (instance.materialOverride) return instance.materialOverrideIdentity;
    return index < instance.materialSlotIdentities.size()
        ? instance.materialSlotIdentities[index] : 0u;
}

bool HasCPUTexture(const FResolvedRenderMaterial& material)
{
    if (!material.source) return false;
    const Material& source = *material.source;
    const int channels = source.texChannels > 0 ? source.texChannels : 3;
    if (source.texWidth <= 0 || source.texHeight <= 0 || channels < 1 || channels > 4)
        return false;
    const std::size_t required = static_cast<std::size_t>(source.texWidth) *
        static_cast<std::size_t>(source.texHeight) * static_cast<std::size_t>(channels);
    return source.texData.size() >= required;
}

void HashMaterial(std::uint64_t& hash, const FResolvedRenderMaterial& material,
                  std::uint32_t identity, const glm::vec2& instanceTiling,
                  std::unordered_set<const Material*>& evaluatedSources,
                  FRaySceneCacheStats& stats)
{
    HashBytes(hash, &identity, sizeof(identity));
    HashBytes(hash, &material.ambient, sizeof(material.ambient));
    HashBytes(hash, &material.albedo, sizeof(material.albedo));
    HashBytes(hash, &material.specularColor, sizeof(material.specularColor));
    HashBytes(hash, &material.emissive, sizeof(material.emissive));
    HashBytes(hash, &material.shininess, sizeof(material.shininess));
    HashBytes(hash, &material.mirrorFactor, sizeof(material.mirrorFactor));
    HashBytes(hash, &material.blendMode, sizeof(material.blendMode));
    HashBytes(hash, &material.opacity, sizeof(material.opacity));
    HashBytes(hash, &material.refraction, sizeof(material.refraction));
    HashBytes(hash, &material.transmittanceColor, sizeof(material.transmittanceColor));
    HashBytes(hash, &material.transmittanceDistance, sizeof(material.transmittanceDistance));
    HashBytes(hash, &material.castRayTracedShadows,
              sizeof(material.castRayTracedShadows));
    HashBytes(hash, &material.runtimeRevision, sizeof(material.runtimeRevision));
    HashBytes(hash, &instanceTiling, sizeof(instanceTiling));
    const std::size_t pathLength = material.diffuseTexturePath.size();
    HashBytes(hash, &pathLength, sizeof(pathLength));
    if (pathLength)
        HashBytes(hash, material.diffuseTexturePath.data(), pathLength);
    if (!material.source) return;
    const Material& source = *material.source;
    if (!evaluatedSources.insert(&source).second) return;
    ++stats.materialTextureMetadataEvaluations;
    const std::uint64_t sourceRuntimeRevision = source.RuntimeRevision();
    HashBytes(hash, &sourceRuntimeRevision, sizeof(sourceRuntimeRevision));
    HashBytes(hash, &source.texture, sizeof(source.texture));
    HashBytes(hash, &source.texWidth, sizeof(source.texWidth));
    HashBytes(hash, &source.texHeight, sizeof(source.texHeight));
    HashBytes(hash, &source.texChannels, sizeof(source.texChannels));
    HashBytes(hash, &source.wrapMode, sizeof(source.wrapMode));
    HashBytes(hash, &source.uvTiling, sizeof(source.uvTiling));
    const std::size_t bytes = source.texData.size();
    HashBytes(hash, &bytes, sizeof(bytes));
    std::error_code stampError;
    const auto stamp = std::filesystem::last_write_time(
        material.diffuseTexturePath, stampError);
    const auto stampValue = stampError ? std::filesystem::file_time_type::duration::rep{} :
        stamp.time_since_epoch().count();
    HashBytes(hash, &stampValue, sizeof(stampValue));
}
} // namespace

const FPackedRayScene& FRaySceneCache::Prepare(const FRenderScene& scene)
{
    packed_.valid = true;
    packed_.diagnostic.clear();
    for (const FRenderMeshInstance& instance : scene.meshes)
    {
        if (!instance.mesh) continue;
        const UMesh& mesh = *instance.mesh;
        if (mesh.vertices.empty())
        {
            packed_.valid = false;
            packed_.diagnostic = "Ray mesh " + std::to_string(mesh.AssetId()) +
                " has no vertices";
            return packed_;
        }
        if (mesh.indices.empty())
        {
            packed_.valid = false;
            packed_.diagnostic = "Ray mesh " + std::to_string(mesh.AssetId()) +
                " has no triangle indices";
            return packed_;
        }
        if (mesh.indices.size() % 3u != 0u)
        {
            packed_.valid = false;
            packed_.diagnostic = "Ray mesh " + std::to_string(mesh.AssetId()) +
                " index count is not a multiple of three";
            return packed_;
        }
        if (mesh.indices.size() / 3u > ExactFloatIntegerLimit)
        {
            packed_.valid = false;
            packed_.diagnostic = "Ray mesh " + std::to_string(mesh.AssetId()) +
                " exceeds the exact 24-bit GL33 traversal-index range";
            return packed_;
        }
        for (std::uint32_t index : mesh.indices)
        {
            if (index >= mesh.vertices.size())
            {
                packed_.valid = false;
                packed_.diagnostic = "Ray mesh " + std::to_string(mesh.AssetId()) +
                    " contains an out of bounds vertex index";
                return packed_;
            }
        }
    }
    if (scene.meshes.size() > ExactFloatIntegerLimit)
    {
        packed_.valid = false;
        packed_.diagnostic =
            "Ray scene exceeds the exact 24-bit GL33 instance-index range";
        return packed_;
    }
    std::unordered_set<std::uint64_t> used;
    std::vector<const FRenderMeshInstance*> validInstances;
    validInstances.reserve(scene.meshes.size());
    bool rebuilt = false;
    for (const FRenderMeshInstance& instance : scene.meshes)
    {
        if (!instance.mesh || instance.mesh->triangleCount() <= 0) continue;
        validInstances.push_back(&instance);
        const std::uint64_t asset = instance.mesh->AssetId();
        used.insert(asset);
        auto found = blas_.find(asset);
        if (found != blas_.end() &&
            found->second.revision == instance.mesh->GeometryRevision()) continue;

        FBLAS built;
        built.revision = instance.mesh->GeometryRevision();
        built.closed = IsClosedTriangleMesh(*instance.mesh);
        BVH bvh;
        bvh.Build(*instance.mesh);
        if (bvh.nodes.size() > ExactFloatIntegerLimit ||
            bvh.triIndices.size() > ExactFloatIntegerLimit)
        {
            packed_.valid = false;
            packed_.diagnostic = "Ray mesh " + std::to_string(asset) +
                " exceeds the exact 24-bit GL33 BVH-index range";
            return packed_;
        }
        built.depth = bvh.Depth();
        built.nodeTexels.reserve(bvh.nodes.size() * 2);
        for (const BVHNode& node : bvh.nodes)
        {
            if (std::abs(static_cast<long long>(node.leftOrTriStart)) >
                    static_cast<long long>(ExactFloatIntegerLimit) ||
                std::abs(static_cast<long long>(node.rightOrTriCount)) >
                    static_cast<long long>(ExactFloatIntegerLimit))
            {
                packed_.valid = false;
                packed_.diagnostic = "Ray mesh " + std::to_string(asset) +
                    " contains a BVH field outside the exact 24-bit GL33 range";
                return packed_;
            }
            built.nodeTexels.emplace_back(node.bbMin,
                static_cast<float>(node.leftOrTriStart));
            built.nodeTexels.emplace_back(node.bbMax,
                static_cast<float>(node.rightOrTriCount));
        }
        built.triangleIndices.reserve(bvh.triIndices.size());
        for (std::uint32_t triangleIndex : bvh.triIndices)
            built.triangleIndices.push_back(static_cast<float>(triangleIndex));
        built.triangleTexels.reserve(static_cast<std::size_t>(
            instance.mesh->triangleCount()) * TexelsPerTriangle);
        glm::vec3 minimum(1.0e30f), maximum(-1.0e30f);
        for (int triangle = 0; triangle < instance.mesh->triangleCount(); ++triangle)
        {
            const Vertex& a = instance.mesh->vertices[instance.mesh->indices[3 * triangle]];
            const Vertex& b = instance.mesh->vertices[instance.mesh->indices[3 * triangle + 1]];
            const Vertex& c = instance.mesh->vertices[instance.mesh->indices[3 * triangle + 2]];
            built.triangleTexels.emplace_back(a.position, a.uv.x);
            built.triangleTexels.emplace_back(b.position, b.uv.x);
            built.triangleTexels.emplace_back(c.position, c.uv.x);
            built.triangleTexels.emplace_back(a.normal, a.uv.y);
            built.triangleTexels.emplace_back(b.normal, b.uv.y);
            built.triangleTexels.emplace_back(c.normal, c.uv.y);
            const std::uint32_t slot = instance.triangleMaterialSlots &&
                static_cast<std::size_t>(triangle) < instance.triangleMaterialSlotCount
                ? (*instance.triangleMaterialSlots)[static_cast<std::size_t>(triangle)]
                : 0u;
            built.triangleTexels.emplace_back(EncodeUInt(slot), 0.0f, 0.0f, 0.0f);
        }
        for (const Vertex& vertex : instance.mesh->vertices)
        {
            minimum = glm::min(minimum, vertex.position);
            maximum = glm::max(maximum, vertex.position);
        }
        if (instance.mesh->vertices.empty()) minimum = maximum = glm::vec3(0.0f);
        built.minimum = minimum;
        built.maximum = maximum;
        if (found == blas_.end())
        {
            blas_.emplace(asset, std::move(built));
            ++stats_.blasBuilds;
        }
        else
        {
            found->second = std::move(built);
            ++stats_.blasRebuilds;
        }
        rebuilt = true;
    }

    for (auto it = blas_.begin(); it != blas_.end();)
    {
        if (!used.count(it->first))
        {
            it = blas_.erase(it);
            ++stats_.blasReleases;
            rebuilt = true;
        }
        else ++it;
    }
    stats_.residentBLAS = blas_.size();

    std::uint64_t setHash = 1469598103934665603ull;
    std::vector<std::uint64_t> orderedAssets;
    for (const FRenderMeshInstance* instance : validInstances)
        if (std::find(orderedAssets.begin(), orderedAssets.end(),
                      instance->mesh->AssetId()) == orderedAssets.end())
            orderedAssets.push_back(instance->mesh->AssetId());
    for (std::uint64_t asset : orderedAssets)
    {
        HashBytes(setHash, &asset, sizeof(asset));
        const std::uint64_t revision = blas_.at(asset).revision;
        HashBytes(setHash, &revision, sizeof(revision));
    }
    packed_.blasChanged = rebuilt || setHash != blasSetHash_;
    packed_.maximumBLASDepth = 0;
    for (std::uint64_t asset : orderedAssets)
        packed_.maximumBLASDepth = std::max(
            packed_.maximumBLASDepth, blas_.at(asset).depth);

    struct FOffset { int node = 0; int triangle = 0; int index = 0; };
    std::unordered_map<std::uint64_t, FOffset> offsets;
    if (packed_.blasChanged)
    {
        packed_.triangleTexels.clear();
        packed_.blasNodeTexels.clear();
        packed_.blasTriangleIndices.clear();
        for (std::uint64_t asset : orderedAssets)
        {
            const FBLAS& blas = blas_.at(asset);
            if (packed_.blasNodeTexels.size() / 2u > ExactFloatIntegerLimit ||
                packed_.triangleTexels.size() / TexelsPerTriangle >
                    ExactFloatIntegerLimit ||
                packed_.blasTriangleIndices.size() > ExactFloatIntegerLimit)
            {
                packed_.valid = false;
                packed_.diagnostic =
                    "Packed ray BLAS offsets exceed the exact 24-bit GL33 range";
                return packed_;
            }
            offsets[asset] = {
                static_cast<int>(packed_.blasNodeTexels.size() / 2),
                static_cast<int>(packed_.triangleTexels.size() / TexelsPerTriangle),
                static_cast<int>(packed_.blasTriangleIndices.size()),
            };
            packed_.triangleTexels.insert(packed_.triangleTexels.end(),
                blas.triangleTexels.begin(), blas.triangleTexels.end());
            packed_.blasNodeTexels.insert(packed_.blasNodeTexels.end(),
                blas.nodeTexels.begin(), blas.nodeTexels.end());
            packed_.blasTriangleIndices.insert(packed_.blasTriangleIndices.end(),
                blas.triangleIndices.begin(), blas.triangleIndices.end());
        }
        packed_.triangleCount = static_cast<int>(
            packed_.triangleTexels.size() / TexelsPerTriangle);
        packed_.blasRevision = nextBLASRevision_++;
        blasSetHash_ = setHash;
    }
    else
    {
        int node = 0, triangle = 0, index = 0;
        for (std::uint64_t asset : orderedAssets)
        {
            const FBLAS& blas = blas_.at(asset);
            offsets[asset] = {node, triangle, index};
            node += static_cast<int>(blas.nodeTexels.size() / 2);
            triangle += static_cast<int>(blas.triangleTexels.size() / TexelsPerTriangle);
            index += static_cast<int>(blas.triangleIndices.size());
        }
    }

    std::uint64_t newInstanceHash = 1469598103934665603ull;
    std::uint64_t newMaterialHash = 1469598103934665603ull;
    std::unordered_set<const Material*> evaluatedMaterialSources;
    for (std::size_t identity = 0; identity < scene.materialsByIdentity.size(); ++identity)
        HashMaterial(newMaterialHash, scene.materialsByIdentity[identity],
            static_cast<std::uint32_t>(identity + 1u), glm::vec2(1.0f),
            evaluatedMaterialSources, stats_);
    for (const FRenderMeshInstance* instance : validInstances)
    {
        const std::uint64_t asset = instance->mesh->AssetId();
        HashBytes(newInstanceHash, &asset, sizeof(asset));
        HashBytes(newInstanceHash, &instance->modelTransform,
                  sizeof(instance->modelTransform));
        HashBytes(newInstanceHash, &instance->objectIdentity,
                  sizeof(instance->objectIdentity));
        const std::size_t materialCount = MaterialCount(*instance);
        const bool overridden = instance->materialOverride.has_value();
        HashBytes(newInstanceHash, &materialCount, sizeof(materialCount));
        HashBytes(newInstanceHash, &overridden, sizeof(overridden));
        HashBytes(newMaterialHash, &asset, sizeof(asset));
        HashBytes(newMaterialHash, &materialCount, sizeof(materialCount));
        for (std::size_t slot = 0; slot < materialCount; ++slot)
            HashMaterial(newMaterialHash, MaterialAt(*instance, slot),
                MaterialIdentityAt(*instance, slot), instance->uvTiling,
                evaluatedMaterialSources, stats_);
    }
    packed_.instancesChanged = packed_.blasChanged ||
        newInstanceHash != instanceHash_;
    if (packed_.instancesChanged)
    {
        packed_.instanceTexels.clear();
        packed_.instanceIdentityTexels.clear();
        std::vector<FTlasBox> boxes;
        int instanceIndex = 0;
        std::uint32_t materialBase = 0;
        for (const FRenderMeshInstance* instance : validInstances)
        {
            const std::uint64_t asset = instance->mesh->AssetId();
            const FBLAS& blas = blas_.at(asset);
            const FOffset offset = offsets.at(asset);
            const std::uint32_t materialCount = static_cast<std::uint32_t>(
                MaterialCount(*instance));
            const std::uint32_t overridden =
                instance->materialOverride.has_value() ? 1u : 0u;
            const glm::mat4 inverse = glm::inverse(instance->modelTransform);
            packed_.instanceTexels.emplace_back(inverse[0][0], inverse[1][0],
                inverse[2][0], inverse[3][0]);
            packed_.instanceTexels.emplace_back(inverse[0][1], inverse[1][1],
                inverse[2][1], inverse[3][1]);
            packed_.instanceTexels.emplace_back(inverse[0][2], inverse[1][2],
                inverse[2][2], inverse[3][2]);
            // The fourth inverse-transform row is unused by affine traversal.
            // Mirror exact identity bits here so GL3.3 can stay within its
            // minimum 16 fragment-texture-unit budget without a separate
            // instance-identity sampler. GL4.3 retains the typed identity SSBO.
            packed_.instanceTexels.emplace_back(EncodeUInt(instance->objectIdentity),
                EncodeUInt(materialBase), EncodeUInt(materialCount),
                EncodeUInt(overridden));
            packed_.instanceTexels.emplace_back(static_cast<float>(offset.node),
                static_cast<float>(offset.triangle), static_cast<float>(offset.index),
                static_cast<float>(instance->mesh->triangleCount()));
            glm::vec3 worldMinimum(1.0e30f), worldMaximum(-1.0e30f);
            for (int corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 local((corner & 1) ? blas.maximum.x : blas.minimum.x,
                    (corner & 2) ? blas.maximum.y : blas.minimum.y,
                    (corner & 4) ? blas.maximum.z : blas.minimum.z);
                const glm::vec3 world = glm::vec3(
                    instance->modelTransform * glm::vec4(local, 1.0f));
                worldMinimum = glm::min(worldMinimum, world);
                worldMaximum = glm::max(worldMaximum, world);
            }
            packed_.instanceTexels.emplace_back(worldMinimum, blas.closed ? 1.0f : 0.0f);
            packed_.instanceTexels.emplace_back(worldMaximum, 0.0f);
            packed_.instanceIdentityTexels.emplace_back(instance->objectIdentity,
                materialBase, materialCount, overridden);
            materialBase += materialCount;
            boxes.push_back({worldMinimum, worldMaximum,
                (worldMinimum + worldMaximum) * 0.5f, instanceIndex++});
        }
        std::vector<BVHNode> nodes;
        std::vector<std::uint32_t> order;
        if (!boxes.empty()) BuildTlasNode(boxes, 0,
            static_cast<int>(boxes.size()), nodes, order);
        if (nodes.size() > ExactFloatIntegerLimit || order.size() > ExactFloatIntegerLimit)
        {
            packed_.valid = false;
            packed_.diagnostic =
                "Ray TLAS exceeds the exact 24-bit GL33 traversal-index range";
            return packed_;
        }
        packed_.tlasNodeTexels.clear();
        for (const BVHNode& node : nodes)
        {
            if (std::abs(static_cast<long long>(node.leftOrTriStart)) >
                    static_cast<long long>(ExactFloatIntegerLimit) ||
                std::abs(static_cast<long long>(node.rightOrTriCount)) >
                    static_cast<long long>(ExactFloatIntegerLimit))
            {
                packed_.valid = false;
                packed_.diagnostic =
                    "Ray TLAS contains a field outside the exact 24-bit GL33 range";
                return packed_;
            }
            packed_.tlasNodeTexels.emplace_back(node.bbMin,
                static_cast<float>(node.leftOrTriStart));
            packed_.tlasNodeTexels.emplace_back(node.bbMax,
                static_cast<float>(node.rightOrTriCount));
        }
        packed_.tlasInstanceIndices.clear();
        packed_.tlasInstanceIndices.reserve(order.size());
        for (std::uint32_t instance : order)
            packed_.tlasInstanceIndices.push_back(static_cast<float>(instance));
        packed_.tlasDepth = nodes.empty() ? 0 : NodeDepth(nodes, 0);
        packed_.instanceCount = static_cast<int>(validInstances.size());
        packed_.instanceRevision = nextInstanceRevision_++;
        instanceHash_ = newInstanceHash;
    }

    packed_.materialsChanged = newMaterialHash != materialHash_;
    if (packed_.materialsChanged)
    {
        packed_.materialTexels.clear();
        packed_.primaryOpticsTexels.clear();
        packed_.textureArrayRGBA.clear();
        packed_.textureWidth = packed_.textureHeight = packed_.textureLayerCount = 0;
        packed_.materialCount = 0;

        std::vector<const Material*> textureSources;
        packed_.primaryOpticsTexels.reserve(scene.materialsByIdentity.size() * 2u);
        for (const FResolvedRenderMaterial& material : scene.materialsByIdentity)
        {
            packed_.primaryOpticsTexels.emplace_back(
                material.transmittanceColor, material.opacity);
            packed_.primaryOpticsTexels.emplace_back(material.refraction,
                material.transmittanceDistance,
                material.blendMode == EMaterialBlendMode::Translucent ? 1.0f : 0.0f,
                material.castRayTracedShadows ? 1.0f : 0.0f);
        }
        for (const FRenderMeshInstance* instance : validInstances)
            for (std::size_t slot = 0; slot < MaterialCount(*instance); ++slot)
            {
                const FResolvedRenderMaterial& material = MaterialAt(*instance, slot);
                if (!HasCPUTexture(material)) continue;
                if (std::find(textureSources.begin(), textureSources.end(), material.source) ==
                    textureSources.end())
                    textureSources.push_back(material.source);
            }
        for (const Material* source : textureSources)
        {
            packed_.textureWidth = std::max(packed_.textureWidth, source->texWidth);
            packed_.textureHeight = std::max(packed_.textureHeight, source->texHeight);
        }
        packed_.textureLayerCount = static_cast<int>(textureSources.size());
        if (!textureSources.empty())
        {
            const std::size_t width = static_cast<std::size_t>(packed_.textureWidth);
            const std::size_t height = static_cast<std::size_t>(packed_.textureHeight);
            if (width > MaxPackedMaterialAtlasBytes / 4u ||
                height > MaxPackedMaterialAtlasBytes / (width * 4u) ||
                textureSources.size() > MaxPackedMaterialAtlasBytes /
                    (width * height * 4u))
            {
                packed_.valid = false;
                packed_.diagnostic =
                    "Ray material atlas exceeds the bounded 256 MiB GL33 packing budget";
                return packed_;
            }
            const std::size_t layerBytes = width * height * 4u;
            packed_.textureArrayRGBA.resize(layerBytes * textureSources.size());
            for (std::size_t layer = 0; layer < textureSources.size(); ++layer)
            {
                const Material& source = *textureSources[layer];
                const std::vector<unsigned char> resampled = ResampleRGBA8ToLayer(
                    source, packed_.textureWidth, packed_.textureHeight);
                std::copy(resampled.begin(), resampled.end(),
                    packed_.textureArrayRGBA.begin() + layer * layerBytes);
            }
        }

        for (const FRenderMeshInstance* instance : validInstances)
            for (std::size_t slot = 0; slot < MaterialCount(*instance); ++slot)
            {
                const FResolvedRenderMaterial& material = MaterialAt(*instance, slot);
                int layer = -1;
                glm::vec2 sourceTiling(1.0f);
                float repeat = 1.0f;
                if (material.source)
                {
                    sourceTiling = material.source->uvTiling;
                    repeat = material.source->wrapMode == EWrapMode::Repeat ? 1.0f : 0.0f;
                    const auto found = std::find(textureSources.begin(), textureSources.end(),
                        material.source);
                    if (found != textureSources.end())
                        layer = static_cast<int>(found - textureSources.begin());
                }
                packed_.materialTexels.emplace_back(material.albedo,
                    material.mirrorFactor);
                packed_.materialTexels.emplace_back(material.ambient,
                    material.shininess);
                packed_.materialTexels.emplace_back(material.specularColor,
                    EncodeUInt(MaterialIdentityAt(*instance, slot)));
                packed_.materialTexels.emplace_back(material.emissive,
                    static_cast<float>(layer));
                packed_.materialTexels.emplace_back(instance->uvTiling * sourceTiling,
                    repeat, 0.0f);
                packed_.materialTexels.emplace_back(0.0f);
                packed_.materialTexels.emplace_back(
                    material.transmittanceColor, material.opacity);
                packed_.materialTexels.emplace_back(material.refraction,
                    material.transmittanceDistance,
                    material.blendMode == EMaterialBlendMode::Translucent ? 1.0f : 0.0f,
                    material.castRayTracedShadows ? 1.0f : 0.0f);
                ++packed_.materialCount;
            }
        packed_.materialRevision = nextMaterialRevision_++;
        materialHash_ = newMaterialHash;
    }
    packed_.warnings.clear();
    std::unordered_set<std::string> warningKeys;
    for (const FRenderMeshInstance* instance : validInstances)
    {
        if (blas_.at(instance->mesh->AssetId()).closed) continue;
        for (std::size_t slot = 0; slot < MaterialCount(*instance); ++slot)
        {
            const FResolvedRenderMaterial& material = MaterialAt(*instance, slot);
            if (material.blendMode != EMaterialBlendMode::Translucent) continue;
            const std::string key = std::to_string(instance->mesh->AssetId()) + ":" +
                std::to_string(MaterialIdentityAt(*instance, slot));
            if (warningKeys.insert(key).second)
                packed_.warnings.push_back("Translucent material " +
                    std::to_string(MaterialIdentityAt(*instance, slot)) + " on mesh " +
                    std::to_string(instance->mesh->AssetId()) +
                    " is not a closed triangle volume; using reflection fallback");
        }
    }
    return packed_;
}

void FRaySceneCache::Clear()
{
    stats_.blasReleases += blas_.size();
    blas_.clear();
    packed_ = {};
    blasSetHash_ = 0;
    instanceHash_ = 0;
    materialHash_ = 0;
    nextBLASRevision_ = 1;
    nextInstanceRevision_ = 1;
    nextMaterialRevision_ = 1;
    stats_.residentBLAS = 0;
}
