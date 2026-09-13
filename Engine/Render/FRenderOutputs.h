#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>

// Logical shading meanings shared by all physical renderer implementations.
// Flat uses a per-primitive geometric normal, Gouraud interpolates vertex-lit
// color, and Phong performs per-pixel lighting from interpolated attributes.
enum class ERenderShadingModel
{
    Flat = 0,
    Gouraud = 1,
    Phong = 2,
};

// Backend-neutral description of one logical G-buffer sample. Tasks 7+ may
// pack these fields into multiple OpenGL textures, but consumers use this
// common vocabulary rather than knowing attachment formats.
struct FLogicalGBufferSample
{
    bool valid = false;
    float coverage = 0.0f;
    float depth = 1.0f;
    glm::vec3 worldPosition = glm::vec3(0.0f);
    glm::vec3 geometricNormal = glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 shadingNormal = glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 resolvedAlbedo = glm::vec3(1.0f);
    glm::vec3 specularColor = glm::vec3(0.0f);
    float shininess = 0.0f;
    float mirrorFactor = 0.0f;
    ERenderShadingModel shadingModel = ERenderShadingModel::Phong;
    std::uint32_t objectIdentity = 0;
    std::uint32_t materialIdentity = 0;
};

// Opaque pass-to-pass surface identity. It intentionally carries no OpenGL
// texture/FBO/SSBO terminology; physical backends map their resources to this
// common view without allocating through the contract itself.
struct FRenderOutputView
{
    std::uint64_t identity = 0;
    int width = 0;
    int height = 0;
    bool valid = false;
};

struct FRasterLightingOutput
{
    bool valid = false;
    FRenderOutputView colorTarget;
    glm::vec3 hdrColor = glm::vec3(0.0f);
};

struct FRayEffectOutputs
{
    std::optional<float> shadows;
    std::optional<glm::vec3> globalIllumination;
    std::optional<glm::vec3> reflections;
    std::optional<FRenderOutputView> shadowVisibilityTarget;
    std::optional<FRenderOutputView> globalIlluminationTarget;
    std::optional<FRenderOutputView> reflectionTarget;

    float ShadowVisibilityOrNeutral() const { return shadows.value_or(1.0f); }
    glm::vec3 GlobalIlluminationOrNeutral() const
    { return globalIllumination.value_or(glm::vec3(0.0f)); }
    glm::vec3 ReflectionOrNeutral() const
    { return reflections.value_or(glm::vec3(0.0f)); }
};

struct FCompositeInput
{
    FRasterLightingOutput rasterLighting;
    FRayEffectOutputs rayEffects;
};

struct FCompositeOutput
{
    bool valid = false;
    FRenderOutputView finalColorTarget;
    glm::vec3 finalColor = glm::vec3(0.0f);
};
