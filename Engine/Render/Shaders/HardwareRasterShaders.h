#pragma once

namespace HardwareRasterShaders
{
inline constexpr const char* Vertex = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalTransform;

out VS_OUT {
    vec3 worldPosition;
    vec3 shadingNormal;
    vec2 uv;
} vsOut;

void main()
{
    vec4 world = uModel * vec4(aPosition, 1.0);
    vsOut.worldPosition = world.xyz;
    vsOut.shadingNormal = uNormalTransform * aNormal;
    vsOut.uv = aUV;
    vec4 clip = uProjection * uView * world;
    // FCG produces negative clip.w for visible -Z points. Homogeneous negation
    // preserves x/y NDC while making the primitive valid for OpenGL clipping;
    // the second z negation maps FCG near=+1/far=-1 to LESS depth.
    clip = -clip;
    clip.z = -clip.z;
    gl_Position = clip;
}
)GLSL";

inline constexpr const char* Geometry = R"GLSL(#version 330 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

in VS_OUT {
    vec3 worldPosition;
    vec3 shadingNormal;
    vec2 uv;
} gsIn[];

const int MAX_POINT_LIGHTS = 16;
uniform int uPointLightCount;
uniform vec3 uPointLightPositions[MAX_POINT_LIGHTS];
uniform vec3 uPointLightRadiances[MAX_POINT_LIGHTS];
uniform vec3 uEye;
uniform vec3 uEnvironmentTint;
uniform vec3 uSkyHorizon;
uniform vec3 uSkyZenith;
uniform float uSkyExponent;
uniform float uAmbientStrength;
uniform vec3 uAlbedo;
uniform vec3 uAmbient;
uniform vec3 uSpecular;
uniform vec3 uEmissive;
uniform float uShininess;
uniform int uShadingModel;
uniform vec2 uUVTiling;
uniform bool uHasDiffuseTexture;
uniform bool uRepeatDiffuseTexture;
uniform sampler2D uDiffuseTexture;
uniform bool uHasEnvironmentTexture;
uniform sampler2D uEnvironmentTexture;

out GS_OUT {
    vec3 worldPosition;
    vec3 shadingNormal;
    vec2 uv;
    flat vec3 geometricNormal;
    vec3 vertexLighting;
    flat vec3 faceLighting;
} gsOut;

vec3 sampledAlbedo(vec2 uv)
{
    vec3 albedo = uAlbedo;
    if (uHasDiffuseTexture)
    {
        vec2 tiled = uv * uUVTiling;
        vec2 addressed = uRepeatDiffuseTexture ? fract(tiled) : clamp(tiled, 0.0, 1.0);
        albedo *= pow(texture(uDiffuseTexture, addressed).rgb, vec3(2.2));
    }
    return albedo;
}

vec3 environmentRadiance(vec3 direction)
{
    float skyK = pow(clamp(direction.y * 0.5 + 0.5, 0.0, 1.0),
                     max(uSkyExponent, 0.01));
    if (!uHasEnvironmentTexture)
        return mix(uSkyHorizon, uSkyZenith, skyK);
    vec3 d = normalize(direction);
    float u = atan(d.z, d.x) * 0.1591549431 + 0.5;
    float v = asin(clamp(d.y, -1.0, 1.0)) * 0.3183098862 + 0.5;
    return textureLod(uEnvironmentTexture, vec2(u, v), 0.0).rgb;
}

vec3 shadeAt(vec3 position, vec3 normal, vec3 albedo)
{
    vec3 N = normalize(normal);
    vec3 result = uAmbient * environmentRadiance(N) *
                  uEnvironmentTint * (0.5 * uAmbientStrength) + uEmissive;
    vec3 V = normalize(uEye - position);
    float exponent = max(uShininess, 1.0);
    for (int i = 0; i < uPointLightCount; ++i)
    {
        vec3 toLight = uPointLightPositions[i] - position;
        float distanceToLight = length(toLight);
        vec3 L = distanceToLight > 1.0e-6 ? toLight / distanceToLight
                                          : vec3(0.0, 1.0, 0.0);
        vec3 H = normalize(L + V);
        result += albedo * uPointLightRadiances[i] * max(dot(N, L), 0.0);
        result += uSpecular * uPointLightRadiances[i] *
                  pow(max(dot(N, H), 0.0), exponent);
    }
    return result;
}

void main()
{
    vec3 edge0 = gsIn[1].worldPosition - gsIn[0].worldPosition;
    vec3 edge1 = gsIn[2].worldPosition - gsIn[0].worldPosition;
    vec3 geometric = normalize(cross(edge0, edge1));
    vec3 reference = gsIn[0].shadingNormal + gsIn[1].shadingNormal + gsIn[2].shadingNormal;
    if (dot(geometric, reference) < 0.0) geometric = -geometric;
    vec3 faceCenter = (gsIn[0].worldPosition + gsIn[1].worldPosition +
                       gsIn[2].worldPosition) / 3.0;
    vec2 centroidUV = (gsIn[0].uv + gsIn[1].uv + gsIn[2].uv) / 3.0;
    vec3 faceLighting = shadeAt(faceCenter, geometric, sampledAlbedo(centroidUV));
    for (int vertex = 0; vertex < 3; ++vertex)
    {
        gl_Position = gl_in[vertex].gl_Position;
        gsOut.worldPosition = gsIn[vertex].worldPosition;
        gsOut.shadingNormal = gsIn[vertex].shadingNormal;
        gsOut.uv = gsIn[vertex].uv;
        gsOut.geometricNormal = geometric;
        gsOut.vertexLighting = shadeAt(gsIn[vertex].worldPosition,
            normalize(gsIn[vertex].shadingNormal), sampledAlbedo(gsIn[vertex].uv));
        gsOut.faceLighting = faceLighting;
        EmitVertex();
    }
    EndPrimitive();
}
)GLSL";

inline constexpr const char* Fragment = R"GLSL(#version 330 core
in GS_OUT {
    vec3 worldPosition;
    vec3 shadingNormal;
    vec2 uv;
    flat vec3 geometricNormal;
    vec3 vertexLighting;
    flat vec3 faceLighting;
} fsIn;

layout(location = 0) out vec4 oPositionCoverage;
layout(location = 1) out vec4 oGeometricNormal;
layout(location = 2) out vec4 oShadingNormalModel;
layout(location = 3) out vec4 oAlbedoShininess;
layout(location = 4) out vec4 oSpecularMirror;
layout(location = 5) out uvec2 oIdentity;
layout(location = 6) out vec4 oPrecomputedLighting;
layout(location = 7) out vec4 oEmissive;

uniform vec3 uAlbedo;
uniform vec3 uAmbient;
uniform vec3 uSpecular;
uniform vec3 uEmissive;
uniform float uShininess;
uniform float uMirrorFactor;
uniform int uShadingModel;
uniform uint uObjectIdentity;
uniform uint uMaterialIdentity;
uniform vec2 uUVTiling;
uniform bool uHasDiffuseTexture;
uniform bool uRepeatDiffuseTexture;
uniform sampler2D uDiffuseTexture;

void main()
{
    vec3 albedo = uAlbedo;
    if (uHasDiffuseTexture)
    {
        vec2 tiled = fsIn.uv * uUVTiling;
        vec2 addressed = uRepeatDiffuseTexture ? fract(tiled) : clamp(tiled, 0.0, 1.0);
        albedo *= pow(texture(uDiffuseTexture, addressed).rgb, vec3(2.2));
    }
    vec3 geometric = normalize(fsIn.geometricNormal);
    vec3 shading = normalize(fsIn.shadingNormal);
    if (uShadingModel == 0) shading = geometric;

    oPositionCoverage = vec4(fsIn.worldPosition, 1.0);
    // The three otherwise-unused alpha channels carry the material ambient
    // coefficient without exceeding OpenGL 3.3's minimum eight MRT limit.
    oGeometricNormal = vec4(geometric, uAmbient.r);
    oShadingNormalModel = vec4(shading, float(uShadingModel));
    oAlbedoShininess = vec4(albedo, uShininess);
    oSpecularMirror = vec4(uSpecular, uMirrorFactor);
    oIdentity = uvec2(uObjectIdentity, uMaterialIdentity);
    vec3 precomputed = uShadingModel == 0 ? fsIn.faceLighting : fsIn.vertexLighting;
    oPrecomputedLighting = vec4(precomputed, uAmbient.g);
    oEmissive = vec4(uEmissive, uAmbient.b);
}
)GLSL";
}
