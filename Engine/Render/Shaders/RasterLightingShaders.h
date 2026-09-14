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

inline constexpr const char* LightingFragment = R"GLSL(#version 330 core
in vec2 vUV;
layout(location = 0) out vec4 oHDRColor;

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
uniform vec3 uPointLightRadiances[MAX_POINT_LIGHTS];

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

vec3 shadePhong(vec3 position, vec3 normal, vec3 albedo,
                vec3 ambientCoefficient, vec3 specular,
                float shininess, vec3 emissive)
{
    vec3 N = normalize(normal);
    float skyK = pow(clamp(N.y * 0.5 + 0.5, 0.0, 1.0),
                     max(uSkyExponent, 0.01));
    vec3 ambientRadiance = uHasSky ? skyColor(N)
                                   : mix(uSkyHorizon, uSkyZenith, skyK);
    vec3 ambient = ambientCoefficient * ambientRadiance *
                   uEnvironmentTint * (0.5 * uAmbientStrength);
    vec3 result = ambient + emissive;
    vec3 V = normalize(uEye - position);
    float exponent = max(shininess, 1.0);
    for (int i = 0; i < uPointLightCount; ++i)
    {
        vec3 toLight = uPointLightPositions[i] - position;
        float distanceToLight = length(toLight);
        vec3 L = distanceToLight > 1.0e-6 ? toLight / distanceToLight
                                          : vec3(0.0, 1.0, 0.0);
        vec3 H = normalize(L + V);
        float ndl = max(dot(N, L), 0.0);
        float ndh = max(dot(N, H), 0.0);
        result += albedo * uPointLightRadiances[i] * ndl;
        result += specular * uPointLightRadiances[i] * pow(ndh, exponent);
    }
    return result;
}

void main()
{
    float depth = texture(uDepth, vUV).r;
    vec4 positionCoverage = texture(uPositionCoverage, vUV);
    if (uDepthView)
    {
        float value = positionCoverage.a > 0.5 ? clamp(1.0 - depth, 0.0, 1.0) : 0.0;
        oHDRColor = vec4(vec3(value), 1.0);
        return;
    }
    if (positionCoverage.a <= 0.5)
    {
        oHDRColor = vec4(skyColor(viewRay(vUV)), 1.0);
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
    if (model == 0 || model == 1)
    {
        // Flat and Gouraud are complete face/vertex-lit results from geometry.
        // Applying environment light here would turn Gouraud into per-pixel shading.
        oHDRColor = vec4(precomputedLighting.rgb, 1.0);
        return;
    }
    vec4 albedoShininess = texture(uAlbedoShininess, vUV);
    vec4 specularMirror = texture(uSpecularMirror, vUV);
    vec3 emissive = emissiveData.rgb;
    oHDRColor = vec4(shadePhong(positionCoverage.xyz, normalModel.xyz,
                                albedoShininess.rgb, ambientCoefficient,
                                specularMirror.rgb, albedoShininess.a,
                                emissive), 1.0);
}
)GLSL";

inline constexpr const char* CompositeFragment = R"GLSL(#version 330 core
in vec2 vUV;
layout(location = 0) out vec4 oColor;
uniform sampler2D uRasterLighting;
uniform sampler2D uShadowVisibility;
uniform sampler2D uGIRadiance;
uniform sampler2D uReflectionRadiance;
uniform bool uHasShadowVisibility;
uniform bool uHasGIRadiance;
uniform bool uHasReflectionRadiance;
void main()
{
    vec3 raster = max(texture(uRasterLighting, vUV).rgb, vec3(0.0));
    // The compatible path has one raster-lighting target, so visibility
    // attenuates that complete raster contribution. GI and reflections are
    // then added in linear HDR space before the one display transform.
    float visibility = uHasShadowVisibility
        ? clamp(texture(uShadowVisibility, vUV).r, 0.0, 1.0) : 1.0;
    vec3 gi = uHasGIRadiance ? texture(uGIRadiance, vUV).rgb : vec3(0.0);
    vec3 reflection = uHasReflectionRadiance
        ? texture(uReflectionRadiance, vUV).rgb : vec3(0.0);
    vec3 linearColor = max(raster * visibility + gi + reflection, vec3(0.0));
    vec3 displayColor = pow(clamp(linearColor, 0.0, 1.0), vec3(1.0 / 2.2));
    oColor = vec4(displayColor, 1.0);
}
)GLSL";
}
