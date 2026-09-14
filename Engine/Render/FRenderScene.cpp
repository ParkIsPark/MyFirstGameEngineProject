#include "FRenderScene.h"

#include "AActor.h"
#include "ACamera.h"
#include "ALight.h"
#include "EnvironmentLightComponent.h"
#include "Material.h"
#include "PointLightComponent.h"
#include "UMesh.h"
#include "UMeshComponent.h"
#include "UScene.h"
#include "UWorld.h"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace
{
ERenderShadingModel ResolveShadingModel(int value)
{
    if (value <= 0) return ERenderShadingModel::Flat;
    if (value == 1) return ERenderShadingModel::Gouraud;
    return ERenderShadingModel::Phong;
}

FResolvedRenderMaterial ResolveMaterial(const Material& material)
{
    FResolvedRenderMaterial result;
    result.source = &material;
    result.ambient = material.ka;
    result.albedo = material.kd;
    result.specularColor = material.ks;
    result.emissive = material.emissive;
    result.shininess = material.shininess;
    result.mirrorFactor = std::max(material.km.x, std::max(material.km.y, material.km.z));
    Material optics;
    optics.blendMode = material.blendMode;
    optics.opacity = material.opacity;
    optics.refraction = material.refraction;
    optics.transmittanceColor = material.transmittanceColor;
    optics.transmittanceDistance = material.transmittanceDistance;
    optics.castRayTracedShadows = material.castRayTracedShadows;
    optics.SanitizeOptics();
    result.blendMode = optics.blendMode;
    result.opacity = optics.opacity;
    result.refraction = optics.refraction;
    result.transmittanceColor = optics.transmittanceColor;
    result.transmittanceDistance = optics.transmittanceDistance;
    result.castRayTracedShadows = optics.castRayTracedShadows;
    result.runtimeRevision = material.RuntimeRevision();
    result.diffuseTexturePath = material.diffuseTexPath;
    return result;
}
} // namespace

FRenderScene ExtractRenderScene(const UWorld& world, const ACamera& camera)
{
    FRenderScene result;
    result.camera.eye = camera.eye;
    result.camera.right = camera.u;
    result.camera.up = camera.v;
    result.camera.backward = camera.w;
    result.camera.left = camera.l;
    result.camera.rightPlane = camera.r;
    result.camera.bottom = camera.b;
    result.camera.top = camera.t;
    result.camera.nearDistance = camera.d;
    result.camera.fovDegrees = camera.fov;

    const UScene& scene = world.GetScene();
    result.environment.skyPath = scene.skyHDRI;
    const ERenderShadingModel shading = ResolveShadingModel(scene.shadingModel);
    result.shadingModel = shading;
    std::unordered_map<const Material*, std::uint32_t> materialIdentities;
    std::uint32_t nextMaterialIdentity = 1;
    std::uint32_t nextObjectIdentity = 1;
    bool environmentPropertiesResolved = false;
    bool actorSkyResolved = false;

    // This is the render request's only actor traversal. All pass consumers use
    // the ordered, immutable values below.
    for (const AActor* actor : scene.Actors)
    {
        if (!actor) continue;

        const UMeshComponent* component = actor->mesh;
        if (component && component->mesh)
        {
            auto identityFor = [&](const Material& material)
            {
                auto inserted = materialIdentities.emplace(&material, nextMaterialIdentity);
                if (inserted.second) ++nextMaterialIdentity;
                return inserted.first->second;
            };

            FRenderMeshInstance instance;
            instance.mesh = component->mesh;
            instance.modelTransform = component->GetWorldMatrix();
            const glm::mat3 model3(instance.modelTransform);
            const float determinant = glm::determinant(model3);
            instance.normalTransform = std::abs(determinant) > 0.000001f
                ? glm::transpose(glm::inverse(model3)) : glm::mat3(1.0f);
            if (const Material* componentOverride = component->EffectiveOverride())
            {
                instance.materialOverride = ResolveMaterial(*componentOverride);
                instance.materialOverrideKind = component->sharedMaterial
                    ? ERenderMaterialOverrideKind::SharedMaterial
                    : ERenderMaterialOverrideKind::ComponentOverride;
                instance.materialOverrideIdentity = identityFor(*componentOverride);
            }
            const std::vector<Material>& slots = component->mesh->materials;
            if (slots.empty())
            {
                instance.materialSlots.push_back(ResolveMaterial(component->mesh->material));
                instance.materialSlotIdentities.push_back(
                    identityFor(component->mesh->material));
            }
            else
            {
                instance.materialSlots.reserve(slots.size());
                instance.materialSlotIdentities.reserve(slots.size());
                for (const Material& slot : slots)
                {
                    instance.materialSlots.push_back(ResolveMaterial(slot));
                    instance.materialSlotIdentities.push_back(identityFor(slot));
                }
            }
            instance.triangleMaterialSlots = &component->mesh->triMaterial;
            instance.triangleMaterialSlotCount = component->mesh->triMaterial.size();
            instance.uvTiling = component->uvTiling;
            instance.shadingModel = shading;
            instance.objectIdentity = nextObjectIdentity++;
            result.meshes.push_back(std::move(instance));
        }

        const ALight* light = dynamic_cast<const ALight*>(actor);
        if (!light || !light->lightComp) continue;
        if (const auto* point = dynamic_cast<const PointLightComponent*>(light->lightComp))
        {
            FRenderPointLight renderLight;
            renderLight.worldPosition = point->GetWorldLocation();
            renderLight.sourceIntensity = point->LightColor * point->LightIntensity;
            result.pointLights.push_back(renderLight);
        }
        else
        {
            if (const auto* environment = dynamic_cast<const EnvironmentLightComponent*>(light->lightComp))
            {
                // The first Environment Light supplies lighting/gradient values,
                // while the first non-empty actor sky path overrides scene sky.
                // These were separate legacy traversals and may select different
                // environment actors.
                if (!environmentPropertiesResolved)
                {
                    result.environment.tint = environment->LightColor * environment->LightIntensity;
                    result.environment.horizon = environment->horizonColor;
                    result.environment.zenith = environment->zenithColor;
                    result.environment.exponent = environment->skyExp;
                    environmentPropertiesResolved = true;
                }
                if (!actorSkyResolved && !environment->skyTexPath.empty())
                {
                    result.environment.skyPath = environment->skyTexPath;
                    actorSkyResolved = true;
                }
            }
        }
    }

    // Preserve the existing Editor/Game Hybrid fallback. The transitional CPU
    // raster executor retains its historical (-4,4,-3) internal default until
    // hardware raster lighting replaces it in Task 8.
    if (result.pointLights.empty())
    {
        result.usesDefaultPointLight = true;
        FRenderPointLight renderLight;
        renderLight.worldPosition = glm::vec3(6.0f, 8.0f, 2.0f);
        renderLight.sourceIntensity = glm::vec3(1.0f);
        result.pointLights.push_back(renderLight);
    }
    return result;
}
