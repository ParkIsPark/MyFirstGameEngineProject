#pragma once

#include <glm/glm.hpp>

struct FRenderQuality;

FRenderQuality SanitizeRenderQuality(FRenderQuality value);
float PointLightAttenuation(float distanceSquared);
float SchlickFresnel(float cosTheta, float n1, float n2);
glm::vec3 BeerLambertFromTransmittance(
    const glm::vec3& transmittanceColor,
    float referenceDistance,
    float travelledDistance);
glm::vec3 ACESFitted(const glm::vec3& linearHDR);
