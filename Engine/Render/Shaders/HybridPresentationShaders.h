#pragma once

namespace HybridPresentationShaders
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

inline constexpr const char* CompositeFragment = R"GLSL(#version 330 core
in vec2 vUV;
layout(location = 0) out vec4 oHDR;
uniform sampler2D uPositionCoverage;
uniform sampler2D uEnvironmentAmbient;
uniform sampler2D uSelectedDirect;
uniform sampler2D uGIRadiance;
uniform sampler2D uOpticalContribution;
uniform sampler2D uEmissive;
uniform bool uHasGIRadiance;
uniform bool uHasOpticalContribution;
void main()
{
    vec3 environmentAmbient = texture(uEnvironmentAmbient, vUV).rgb;
    if (texture(uPositionCoverage, vUV).a <= 0.5)
    {
        oHDR = vec4(environmentAmbient, 1.0);
        return;
    }
    vec3 direct = texture(uSelectedDirect, vUV).rgb;
    vec3 gi = uHasGIRadiance ? texture(uGIRadiance, vUV).rgb : vec3(0.0);
    vec3 emissive = texture(uEmissive, vUV).rgb;
    vec4 optical = uHasOpticalContribution
        ? texture(uOpticalContribution, vUV) : vec4(0.0, 0.0, 0.0, 1.0);
    oHDR = vec4(emissive + optical.a * (environmentAmbient + direct + gi) +
                optical.rgb, 1.0);
}
)GLSL";

inline constexpr const char* PresentationFragment = R"GLSL(#version 330 core
in vec2 vUV;
layout(location = 0) out vec4 oColor;
uniform sampler2D uHDRInput;
uniform float uExposureEV;
uniform bool uDepthView;
vec3 linearToSRGB(vec3 x)
{
    bvec3 cutoff = lessThanEqual(x, vec3(0.0031308));
    vec3 low = 12.92 * x;
    vec3 high = 1.055 * pow(max(x, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, cutoff);
}
vec3 acesFitted(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
void main()
{
    vec3 linearColor = texture(uHDRInput, vUV).rgb;
    vec3 mapped = uDepthView
        ? linearColor
        : acesFitted(linearColor * exp2(uExposureEV));
    oColor = vec4(linearToSRGB(mapped), 1.0);
}
)GLSL";
} // namespace HybridPresentationShaders
