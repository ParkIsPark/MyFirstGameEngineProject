#pragma once

#include "HardwareRasterShaders.h"
#include "RayEffectsComputeShaders.h"
#include "RayEffectsFragmentShaders.h"
#include "RasterLightingShaders.h"

#include <string>

namespace SharedLightingShaderSource
{
inline std::string PointLightFunctions()
{
    return R"GLSL(
void evaluatePointLight(vec3 position, vec3 normal, vec3 viewDirection,
                        vec3 albedo, vec3 specularColor, float shininess,
                        vec3 lightPosition, vec3 lightSource,
                        out vec3 diffuse, out vec3 specular)
{
    vec3 toLight = lightPosition - position;
    float rawDistanceSquared = dot(toLight, toLight);
    float distanceSquared = max(rawDistanceSquared, 0.01);
    vec3 L = rawDistanceSquared > 1.0e-12
        ? toLight * inversesqrt(rawDistanceSquared) : vec3(0.0, 1.0, 0.0);
    vec3 H = normalize(L + viewDirection);
    vec3 incident = lightSource / distanceSquared;
    diffuse = albedo * incident * max(dot(normal, L), 0.0);
    specular = specularColor * incident *
        pow(max(dot(normal, H), 0.0), max(shininess, 1.0));
}
)GLSL";
}

inline std::string BuildHardwareGeometryShader()
{
    return std::string(HardwareRasterShaders::GeometryBeforePointLight) +
        PointLightFunctions() + HardwareRasterShaders::GeometryAfterPointLight;
}

inline std::string BuildRasterLightingFragmentShader()
{
    return std::string(RasterLightingShaders::LightingBeforePointLight) +
        PointLightFunctions() + RasterLightingShaders::LightingAfterPointLight;
}

inline std::string BuildRayEffectsFragmentShader()
{
    return std::string(RayEffectsFragmentShaders::EffectsBeforePointLight) +
        PointLightFunctions() + RayEffectsFragmentShaders::EffectsAfterPointLight;
}

inline std::string BuildRayEffectsComputeShader()
{
    return std::string(RayEffectsComputeShaders::EffectsBeforePointLight) +
        PointLightFunctions() + RayEffectsComputeShaders::EffectsAfterPointLight;
}
}
