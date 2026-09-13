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

out GS_OUT {
    vec3 worldPosition;
    vec3 shadingNormal;
    vec2 uv;
    flat vec3 geometricNormal;
} gsOut;

void main()
{
    vec3 edge0 = gsIn[1].worldPosition - gsIn[0].worldPosition;
    vec3 edge1 = gsIn[2].worldPosition - gsIn[0].worldPosition;
    vec3 geometric = normalize(cross(edge0, edge1));
    vec3 reference = gsIn[0].shadingNormal + gsIn[1].shadingNormal + gsIn[2].shadingNormal;
    if (dot(geometric, reference) < 0.0) geometric = -geometric;
    for (int vertex = 0; vertex < 3; ++vertex)
    {
        gl_Position = gl_in[vertex].gl_Position;
        gsOut.worldPosition = gsIn[vertex].worldPosition;
        gsOut.shadingNormal = gsIn[vertex].shadingNormal;
        gsOut.uv = gsIn[vertex].uv;
        gsOut.geometricNormal = geometric;
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
} fsIn;

layout(location = 0) out vec4 oPositionCoverage;
layout(location = 1) out vec4 oGeometricNormal;
layout(location = 2) out vec4 oShadingNormalModel;
layout(location = 3) out vec4 oAlbedoShininess;
layout(location = 4) out vec4 oSpecularMirror;
layout(location = 5) out uvec2 oIdentity;

uniform vec3 uAlbedo;
uniform vec3 uSpecular;
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
    oGeometricNormal = vec4(geometric, 0.0);
    oShadingNormalModel = vec4(shading, float(uShadingModel));
    oAlbedoShininess = vec4(albedo, uShininess);
    oSpecularMirror = vec4(uSpecular, uMirrorFactor);
    oIdentity = uvec2(uObjectIdentity, uMaterialIdentity);
}
)GLSL";
}
