#include "FRenderMath.h"

#include "FRenderQuality.h"

#include <algorithm>
#include <cmath>

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
