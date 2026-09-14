#pragma once

namespace RasterLightingShaders
{
inline constexpr const char* FullscreenVertex = R"GLSL(#version 330 core
out vec2 vUV;
void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

inline constexpr const char* LightingBeforePointLight = R"GLSL(#version 330 core
in vec2 vUV;
layout(location = 0) out vec4 oEnvironmentAmbient;
layout(location = 1) out vec4 oUnshadowedDirect;
uniform sampler2D uPositionCoverage;
uniform sampler2D uGeometricNormal;
uniform sampler2D uShadingNormalModel;
uniform sampler2D uAlbedoShininess;
uniform sampler2D uSpecularMirror;
uniform usampler2D uIdentity;
uniform sampler2D uPrecomputedLighting;
uniform sampler2D uEmissive;
uniform sampler2D uDepth;
uniform sampler2D uSky;
uniform bool uHasSky;
uniform bool uDepthView;
uniform vec3 uEye;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform vec3 uCameraBackward;
uniform vec4 uFrustum;
uniform float uNearDistance;
uniform vec3 uEnvironmentTint;
uniform vec3 uSkyHorizon;
uniform vec3 uSkyZenith;
uniform float uSkyExponent;
uniform float uAmbientStrength;
const int MAX_POINT_LIGHTS = 16;
uniform int uPointLightCount;
uniform vec3 uPointLightPositions[MAX_POINT_LIGHTS];
uniform vec3 uPointLightSources[MAX_POINT_LIGHTS];
)GLSL";

inline constexpr const char* LightingAfterPointLight = R"GLSL(
vec3 viewRay(vec2 uv)
{
    float x = mix(uFrustum.x, uFrustum.y, uv.x);
    float y = mix(uFrustum.z, uFrustum.w, uv.y);
    return normalize(-uNearDistance * uCameraBackward + x * uCameraRight + y * uCameraUp);
}
vec3 gradientSky(vec3 direction)
{
    float k = pow(clamp(direction.y * 0.5 + 0.5, 0.0, 1.0),
                  max(uSkyExponent, 0.01));
    return mix(uSkyHorizon, uSkyZenith, k);
}
vec3 skyColor(vec3 direction)
{
    if (!uHasSky) return gradientSky(direction);
    vec3 d = normalize(direction);
    float u = atan(d.z, d.x) * 0.1591549431 + 0.5;
    float v = asin(clamp(d.y, -1.0, 1.0)) * 0.3183098862 + 0.5;
    return texture(uSky, vec2(u, v)).rgb;
}
vec3 ambientAt(vec3 normal, vec3 ambientCoefficient)
{
    vec3 N = normalize(normal);
    return ambientCoefficient * skyColor(N) * uEnvironmentTint *
           (0.5 * uAmbientStrength);
}
vec3 phongDirectAt(vec3 position, vec3 normal, vec3 albedo,
                   vec3 specularColor, float shininess)
{
    vec3 N = normalize(normal);
    vec3 V = normalize(uEye - position);
    vec3 direct = vec3(0.0);
    for (int i = 0; i < uPointLightCount; ++i)
    {
        vec3 diffuse;
        vec3 specular;
        evaluatePointLight(position, N, V, albedo, specularColor, shininess,
                           uPointLightPositions[i], uPointLightSources[i],
                           diffuse, specular);
        direct += diffuse + specular;
    }
    return direct;
}
void main()
{
    float depth = texture(uDepth, vUV).r;
    vec4 positionCoverage = texture(uPositionCoverage, vUV);
    if (uDepthView)
    {
        float value = positionCoverage.a > 0.5 ? clamp(1.0 - depth, 0.0, 1.0) : 0.0;
        oEnvironmentAmbient = vec4(vec3(value), 1.0);
        oUnshadowedDirect = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    if (positionCoverage.a <= 0.5)
    {
        oEnvironmentAmbient = vec4(skyColor(viewRay(vUV)), 1.0);
        oUnshadowedDirect = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec4 normalModel = texture(uShadingNormalModel, vUV);
    vec4 geometricNormal = texture(uGeometricNormal, vUV);
    vec4 precomputedLighting = texture(uPrecomputedLighting, vUV);
    vec4 emissiveData = texture(uEmissive, vUV);
    vec3 ambientCoefficient = vec3(geometricNormal.a,
                                   precomputedLighting.a,
                                   emissiveData.a);
    int model = int(normalModel.w + 0.5);
    vec3 ambientNormal = model == 0 ? geometricNormal.xyz : normalModel.xyz;
    oEnvironmentAmbient = vec4(
        ambientAt(ambientNormal, ambientCoefficient), 1.0);
    if (model == 0 || model == 1)
    {
        oUnshadowedDirect = vec4(precomputedLighting.rgb, 1.0);
        return;
    }
    vec4 albedoShininess = texture(uAlbedoShininess, vUV);
    vec4 specularMirror = texture(uSpecularMirror, vUV);
    oUnshadowedDirect = vec4(phongDirectAt(
        positionCoverage.xyz, normalModel.xyz, albedoShininess.rgb,
        specularMirror.rgb, albedoShininess.a), 1.0);
}
)GLSL";

}
