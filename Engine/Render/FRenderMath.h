#pragma once

#include <glm/glm.hpp>

struct FRenderQuality;
class UMesh;

struct FMaterialOpticalWeights
{
    float local = 1.0f;
    float mirror = 0.0f;
    float transmission = 0.0f;
};

FRenderQuality SanitizeRenderQuality(FRenderQuality value);
float PointLightAttenuation(float distanceSquared);
float SchlickFresnel(float cosTheta, float n1, float n2);
glm::vec3 BeerLambertFromTransmittance(
    const glm::vec3& transmittanceColor,
    float referenceDistance,
    float travelledDistance);
glm::vec3 ACESFitted(const glm::vec3& linearHDR);
FMaterialOpticalWeights ResolveMaterialOpticalWeights(
    bool translucent, float opacity, float mirrorFactor,
    float reflectionStrength, bool rayEffectsAvailable);
bool IsClosedTriangleMesh(const UMesh& mesh);
