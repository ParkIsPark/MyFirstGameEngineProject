#pragma once

#include "FRenderOutputs.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class ACamera;
struct Material;
class UMesh;
class UWorld;

struct FRenderCamera
{
    glm::vec3 eye = glm::vec3(0.0f);
    glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 backward = glm::vec3(0.0f, 0.0f, 1.0f);
    float left = -1.0f;
    float rightPlane = 1.0f;
    float bottom = -1.0f;
    float top = 1.0f;
    float nearDistance = 1.0f;
    float fovDegrees = 60.0f;
};

struct FResolvedRenderMaterial
{
    // Borrowed for this Render call only. Large CPU texture data remains owned
    // by the source material and is never copied into the per-frame snapshot.
    const Material* source = nullptr;
    glm::vec3 ambient = glm::vec3(0.2f);
    glm::vec3 albedo = glm::vec3(1.0f);
    glm::vec3 specularColor = glm::vec3(0.0f);
    glm::vec3 emissive = glm::vec3(0.0f);
    float shininess = 0.0f;
    float mirrorFactor = 0.0f;
    EMaterialBlendMode blendMode = EMaterialBlendMode::Opaque;
    float opacity = 1.0f;
    float refraction = 1.52f;
    glm::vec3 transmittanceColor = glm::vec3(1.0f);
    float transmittanceDistance = 1.0f;
    bool castRayTracedShadows = true;
    std::uint64_t runtimeRevision = 0;
    std::string diffuseTexturePath;
};

enum class ERenderMaterialOverrideKind
{
    None,
    ComponentOverride,
    SharedMaterial,
};

struct FRenderMeshInstance
{
    const UMesh* mesh = nullptr;
    glm::mat4 modelTransform = glm::mat4(1.0f);
    glm::mat3 normalTransform = glm::mat3(1.0f);
    // Component-wide shared/override material. Empty means the mesh's
    // triMaterial mapping must select from materialSlots.
    std::optional<FResolvedRenderMaterial> materialOverride;
    ERenderMaterialOverrideKind materialOverrideKind =
        ERenderMaterialOverrideKind::None;
    std::uint32_t materialOverrideIdentity = 0;
    // Scalar/path snapshots only. Each source is borrowed for texture bytes;
    // no Material object or texData vector is copied per frame.
    std::vector<FResolvedRenderMaterial> materialSlots;
    std::vector<std::uint32_t> materialSlotIdentities;
    // Borrowed mapping view for this render call. GeometryRevision is the
    // explicit invalidation contract; copying this potentially large vector
    // every frame would defeat snapshot extraction's bounded cost.
    const std::vector<std::uint32_t>* triangleMaterialSlots = nullptr;
    std::size_t triangleMaterialSlotCount = 0;
    glm::vec2 uvTiling = glm::vec2(1.0f);
    ERenderShadingModel shadingModel = ERenderShadingModel::Phong;
    std::uint32_t objectIdentity = 0;
};

struct FRenderPointLight
{
    glm::vec3 worldPosition = glm::vec3(0.0f);
    // Unattenuated lightColor * lightIntensity. Consumers apply distance.
    glm::vec3 sourceIntensity = glm::vec3(1.0f);
};

struct FRenderEnvironment
{
    std::string skyPath;
    glm::vec3 tint = glm::vec3(1.0f);
    glm::vec3 horizon = glm::vec3(0.10f, 0.12f, 0.16f);
    glm::vec3 zenith = glm::vec3(0.40f, 0.55f, 0.80f);
    float exponent = 1.0f;
};

struct FRenderScene
{
    FRenderCamera camera;
    ERenderShadingModel shadingModel = ERenderShadingModel::Phong;
    std::vector<FRenderMeshInstance> meshes;
    std::vector<FRenderPointLight> pointLights;
    bool usesDefaultPointLight = false;
    FRenderEnvironment environment;
};

FRenderScene ExtractRenderScene(const UWorld& world, const ACamera& camera);
