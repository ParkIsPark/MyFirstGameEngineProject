#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Both physical ray backends upload these arrays verbatim. vec4/uvec4 have
// the exact std430 array stride required by GLSL; scalar float arrays use the
// std430 scalar stride. Buffer object base addresses provide the base alignment.
static_assert(sizeof(glm::vec4) == 16, "packed ray vec4 must match std430");
static_assert(sizeof(glm::uvec4) == 16, "packed ray uvec4 must match std430");
static_assert(sizeof(float) == 4, "packed ray scalar must be IEEE-width float");
static_assert(sizeof(glm::vec4) == 4 * sizeof(float),
              "packed ray vec4 array stride must match std430");
static_assert(sizeof(glm::uvec4) == 4 * sizeof(std::uint32_t),
              "packed ray uvec4 array stride must match std430");

struct FRenderScene;

struct FPackedRayScene
{
    bool valid = true;
    std::string diagnostic;
    std::vector<glm::vec4> triangleTexels;
    std::vector<glm::vec4> blasNodeTexels;
    std::vector<float> blasTriangleIndices;
    // Seven texels per instance. Texel 3 mirrors exact identity bits for the
    // sampler-limited GL3.3 path; GL4.3 also consumes the typed vector below.
    std::vector<glm::vec4> instanceTexels;
    std::vector<glm::uvec4> instanceIdentityTexels;
    std::vector<glm::vec4> tlasNodeTexels;
    std::vector<float> tlasInstanceIndices;
    // Eight texels per deterministic material record: diffuse/mirror,
    // ambient/shininess, specular/exact identity bits, emissive/atlas layer,
    // combined UV tiling/wrap metadata, and logical source texture size.
    std::vector<glm::vec4> materialTexels;
    // Two texels per dense primary material identity (identity N -> N-1).
    std::vector<glm::vec4> primaryOpticsTexels;
    std::vector<std::string> warnings;
    std::vector<unsigned char> textureArrayRGBA;
    int textureWidth = 0;
    int textureHeight = 0;
    int textureLayerCount = 0;
    int materialCount = 0;
    int triangleCount = 0;
    int instanceCount = 0;
    int maximumBLASDepth = 0;
    int tlasDepth = 0;
    std::uint64_t blasRevision = 0;
    std::uint64_t instanceRevision = 0;
    std::uint64_t materialRevision = 0;
    bool blasChanged = false;
    bool instancesChanged = false;
    bool materialsChanged = false;
};

struct FRaySceneCacheStats
{
    std::uint64_t blasBuilds = 0;
    std::uint64_t blasRebuilds = 0;
    std::uint64_t blasReleases = 0;
    std::size_t residentBLAS = 0;
};

// CPU-side deterministic two-level acceleration packing shared by GL backends.
// BLAS entries are keyed by stable mesh asset id + geometry revision; instance
// transforms and the TLAS are independently repacked when their hash changes.
class FRaySceneCache
{
public:
    const FPackedRayScene& Prepare(const FRenderScene& scene);
    void Clear();
    const FRaySceneCacheStats& Stats() const { return stats_; }

private:
    struct FBLAS
    {
        std::uint64_t revision = 0;
        std::vector<glm::vec4> triangleTexels;
        std::vector<glm::vec4> nodeTexels;
        std::vector<float> triangleIndices;
        int depth = 0;
        bool closed = false;
        glm::vec3 minimum = glm::vec3(0.0f);
        glm::vec3 maximum = glm::vec3(0.0f);
    };
    std::unordered_map<std::uint64_t, FBLAS> blas_;
    FPackedRayScene packed_;
    std::uint64_t blasSetHash_ = 0;
    std::uint64_t instanceHash_ = 0;
    std::uint64_t materialHash_ = 0;
    std::uint64_t nextBLASRevision_ = 1;
    std::uint64_t nextInstanceRevision_ = 1;
    std::uint64_t nextMaterialRevision_ = 1;
    FRaySceneCacheStats stats_;
};
