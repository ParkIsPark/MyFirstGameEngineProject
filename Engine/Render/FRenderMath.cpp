#include "FRenderMath.h"

#include "FRenderQuality.h"
#include "UMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>

FRenderQuality SanitizeRenderQuality(FRenderQuality value)
{
    value.ssaa = value.ssaa <= 1 ? 1 : 2;
    value.shadowSamples = std::clamp(value.shadowSamples, 1, 16);
    value.giSamples = std::clamp(value.giSamples, 0, 32);
    value.giBounces = std::clamp(value.giBounces, 0, 4);
    value.temporalFrames = std::clamp(value.temporalFrames, 1, 32);
    value.anisotropy = std::clamp(value.anisotropy, 1.0f, 16.0f);
    value.reflStrength = std::clamp(value.reflStrength, 0.0f, 1.0f);
    value.exposureEV = std::isfinite(value.exposureEV)
        ? std::clamp(value.exposureEV, -16.0f, 16.0f)
        : 0.0f;
    return value;
}

float PointLightAttenuation(float distanceSquared)
{
    return 1.0f / std::max(distanceSquared, 0.01f);
}

float SchlickFresnel(float cosTheta, float n1, float n2)
{
    const float ratio = (n1 - n2) / (n1 + n2);
    const float f0 = ratio * ratio;
    const float oneMinusCosine = 1.0f - std::clamp(cosTheta, 0.0f, 1.0f);
    return f0 + (1.0f - f0) * std::pow(oneMinusCosine, 5.0f);
}

glm::vec3 BeerLambertFromTransmittance(
    const glm::vec3& transmittanceColor,
    float referenceDistance,
    float travelledDistance)
{
    const glm::vec3 clamped = glm::clamp(
        transmittanceColor, glm::vec3(0.0001f), glm::vec3(1.0f));
    const float distance = std::max(referenceDistance, 0.0001f);
    const glm::vec3 sigmaA = -glm::log(clamped) / distance;
    return glm::exp(-sigmaA * std::max(travelledDistance, 0.0f));
}

glm::vec3 ACESFitted(const glm::vec3& x)
{
    constexpr float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return glm::clamp((x * (a * x + b)) / (x * (c * x + d) + e),
                      glm::vec3(0.0f), glm::vec3(1.0f));
}

FMaterialOpticalWeights ResolveMaterialOpticalWeights(
    bool translucent, float opacity, float mirrorFactor,
    float reflectionStrength, bool rayEffectsAvailable)
{
    if (!rayEffectsAvailable) return {};
    FMaterialOpticalWeights result;
    result.transmission = translucent
        ? 1.0f - std::clamp(opacity, 0.0f, 1.0f) : 0.0f;
    result.mirror = (1.0f - result.transmission) *
        std::clamp(mirrorFactor * reflectionStrength, 0.0f, 1.0f);
    result.local = 1.0f - result.transmission - result.mirror;
    return result;
}

bool IsClosedTriangleMesh(const UMesh& mesh)
{
    if (mesh.indices.empty() || mesh.indices.size() % 3u != 0u) return false;
    struct FPositionKey
    {
        std::int64_t x, y, z;
        bool operator==(const FPositionKey& other) const
        { return x == other.x && y == other.y && z == other.z; }
    };
    struct FPositionHash
    {
        std::size_t operator()(const FPositionKey& key) const
        {
            std::size_t h = std::hash<std::int64_t>{}(key.x);
            h ^= std::hash<std::int64_t>{}(key.y) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<std::int64_t>{}(key.z) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };
    struct FEdgeKey
    {
        std::uint32_t a, b;
        bool operator==(const FEdgeKey& other) const { return a == other.a && b == other.b; }
    };
    struct FEdgeHash
    {
        std::size_t operator()(const FEdgeKey& edge) const
        {
            std::size_t h = std::hash<std::uint32_t>{}(edge.a);
            h ^= std::hash<std::uint32_t>{}(edge.b) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };
    constexpr double PositionEpsilon = 1.0e-5;
    std::unordered_map<FPositionKey, std::vector<std::uint32_t>, FPositionHash> cells;
    std::vector<glm::vec3> canonicalPositions;
    std::vector<std::uint32_t> ids(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        const glm::vec3 p = mesh.vertices[i].position;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
        const FPositionKey key{static_cast<std::int64_t>(std::floor(p.x / PositionEpsilon)),
            static_cast<std::int64_t>(std::floor(p.y / PositionEpsilon)),
            static_cast<std::int64_t>(std::floor(p.z / PositionEpsilon))};
        std::uint32_t match = std::numeric_limits<std::uint32_t>::max();
        for (std::int64_t dz = -1; dz <= 1; ++dz)
            for (std::int64_t dy = -1; dy <= 1; ++dy)
                for (std::int64_t dx = -1; dx <= 1; ++dx)
                {
                    const auto found = cells.find({key.x + dx, key.y + dy, key.z + dz});
                    if (found == cells.end()) continue;
                    for (const std::uint32_t candidate : found->second)
                    {
                        const glm::vec3 delta = canonicalPositions[candidate] - p;
                        if (glm::dot(delta, delta) <= PositionEpsilon * PositionEpsilon)
                            match = std::min(match, candidate);
                    }
                }
        if (match == std::numeric_limits<std::uint32_t>::max())
        {
            match = static_cast<std::uint32_t>(canonicalPositions.size());
            canonicalPositions.push_back(p);
            cells[key].push_back(match);
        }
        ids[i] = match;
    }
    std::unordered_map<FEdgeKey, unsigned, FEdgeHash> incidence;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3u)
    {
        if (mesh.indices[i] >= ids.size() || mesh.indices[i + 1] >= ids.size() ||
            mesh.indices[i + 2] >= ids.size()) return false;
        const std::uint32_t tri[3] = {ids[mesh.indices[i]], ids[mesh.indices[i + 1]],
                                      ids[mesh.indices[i + 2]]};
        if (tri[0] == tri[1] || tri[1] == tri[2] || tri[2] == tri[0]) return false;
        for (int edge = 0; edge < 3; ++edge)
        {
            const std::uint32_t a = std::min(tri[edge], tri[(edge + 1) % 3]);
            const std::uint32_t b = std::max(tri[edge], tri[(edge + 1) % 3]);
            ++incidence[{a, b}];
        }
    }
    return std::all_of(incidence.begin(), incidence.end(),
        [](const auto& edge) { return edge.second == 2u; });
}
