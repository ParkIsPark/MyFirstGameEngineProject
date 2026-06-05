#pragma once
// ---------------------------------------------------------------------------
// RTShading.h — the SHARED ray-traced lighting + shadow stage.
//
// This GLSL is the single source of truth for how a visible surface is lit:
// Blinn-Phong direct light + sky-hemisphere ambient + one BVH-traced hard
// shadow ray, then Reinhard tone map + gamma. It is concatenated into BOTH:
//
//   * UMeshRayTracer  (GPU ray-trace mode) — primary hit from a camera ray
//   * UHybridPass     (hybrid mode)        — primary hit read from the G-buffer
//
// i.e. the hybrid renderer rasterizes to decide WHAT is visible, then runs this
// exact same ray-traced pass to decide how it is LIT and shadowed. Keeping one
// copy guarantees the two modes can never visually drift apart again.
//
// Each shader must declare, BEFORE this block, the uniforms it references:
//   uniform samplerBuffer uNodes;   // BVH nodes  (2 RGBA32F texels/node)
//   uniform samplerBuffer uTriIdx;  // BVH leaf -> triangle index (R32F)
//   uniform vec3  uEye, uKs;
//   uniform float uShininess;
//   #define MAX_LIGHTS 8
//   uniform int   uNumLights;                  // active light count (<= MAX_LIGHTS)
//   uniform vec3  uLightPosArr[MAX_LIGHTS];
//   uniform vec3  uLightColorArr[MAX_LIGHTS];
// and define a triangle-position accessor (stride differs per mode):
//   vec3 triPos(int tri, int slot);   // slot 0..2 -> the three vertices
// ---------------------------------------------------------------------------

static const char* RT_SHADING_GLSL = R"GLSL(
// ---- shared ray-traced shading (GPU-RT and Hybrid use this identical code) --

uniform sampler2D uSky;     // equirectangular HDRI (sampled when uHasSky != 0)
uniform int       uHasSky;
// Environment-light driven sky gradient (when no HDRI) + GI controls.
uniform vec3  uSkyHorizon;  // horizon color
uniform vec3  uSkyZenith;   // zenith color
uniform float uSkyExp;      // gradient curve exponent
uniform int   uGISamples;   // hemisphere GI samples (0 = flat ambient)
uniform vec3  uEnvTint;     // environment light color * intensity (scales GI)
uniform int   uShadowSamples; // soft-shadow rays per light (1 = hard shadow)
uniform float uShadowSoftness;// penumbra radius (light angular size)
uniform int   uGIBounces;     // GI path bounces (1 = sky only; >1 = color bleed)

// Radiance gathered along a GI sample ray (per-render-mode: GPU RT path-traces
// uGIBounces bounces; Hybrid has no traceClosest -> sky-or-nothing). Declared
// here so the shared shadeSurface can call it; defined by each pass.
vec3 giSampleRadiance(vec3 ro, vec3 dir);

// Procedural gradient sky (env-light colors). Used for the matte ambient term so
// a flat (non-GI) surface never shows the HDRI image directly.
vec3 gradientSky(vec3 rd) {
    float k = pow(clamp(rd.y * 0.5 + 0.5, 0.0, 1.0), max(uSkyExp, 0.01));
    return mix(uSkyHorizon, uSkyZenith, k);
}
// Full sky: the HDRI when set (background, mirror reflection, GI gathering),
// else the gradient.
vec3 skyColor(vec3 rd) {
    if (uHasSky != 0) {
        vec3 d = normalize(rd);
        float u = atan(d.z, d.x) * 0.15915494 + 0.5;            // 1/(2*pi)
        float v = asin(clamp(d.y, -1.0, 1.0)) * 0.31830989 + 0.5; // 1/pi
        return texture(uSky, vec2(u, v)).rgb;
    }
    return gradientSky(rd);
}

// --- hemisphere GI helpers (Unreal-Lumen-style environment lighting + AO) ---
float _giHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void  _giBasis(vec3 n, out vec3 t, out vec3 b) {
    vec3 up = abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    t = normalize(cross(up, n)); b = cross(n, t);
}
vec3 _giCosHemi(vec3 n, float u1, float u2) {
    float r = sqrt(u1); float phi = 6.2831853 * u2;
    vec3 t, b; _giBasis(n, t, b);
    return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - u1)));
}

// Moller-Trumbore, occlusion variant (only the hit distance matters).
bool _rayTriT(vec3 ro, vec3 rd, vec3 v0, vec3 v1, vec3 v2, out float t) {
    vec3 e1 = v1 - v0, e2 = v2 - v0;
    vec3 p = cross(rd, e2);
    float det = dot(e1, p);
    if (abs(det) < 1e-8) return false;
    float inv = 1.0 / det;
    vec3 s = ro - v0;
    float u = dot(s, p) * inv;   if (u < 0.0 || u > 1.0) return false;
    vec3 q = cross(s, e1);
    float v = dot(rd, q) * inv;  if (v < 0.0 || u + v > 1.0) return false;
    t = dot(e2, q) * inv;        return t > 1e-4;
}

// Ray vs AABB slab (per-axis min/max avoids 0*inf NaN on axis-parallel rays).
bool _slab(vec3 ro, vec3 invD, vec3 mn, vec3 mx, float tMax) {
    vec3 t0 = (mn - ro) * invD, t1 = (mx - ro) * invD;
    vec3 te = min(t0, t1), tx = max(t0, t1);
    float enter = max(max(te.x, te.y), max(te.z, 0.0));
    float exit  = min(min(tx.x, tx.y), min(tx.z, tMax));
    return enter <= exit;
}

// Any-hit occlusion up to maxT. tMin ignores hits closer than that along the ray
// -- essential for GI/AO rays, whose origin sits on the surface: without it,
// grazing samples hit the originating mesh itself and produce black self-
// occlusion speckles (acne). The traversal DEFINITION is provided per render pass
// (declared here so shadeSurface/directLight below can call it): the hybrid pass
// traverses one world-space BVH; the GPU-RT pass traverses a two-level BVH
// (instance loop -> per-mesh BLAS in local space) so moving an object never
// rebuilds the whole tree. Only this prototype is shared.
bool occluded(vec3 ro, vec3 rd, float maxT, float tMin);
// Convenience: shadow rays use the default tiny bias.
bool occluded(vec3 ro, vec3 rd, float maxT) { return occluded(ro, rd, maxT, 1e-4); }

// Reinhard tone map + gamma: compress accumulated light instead of clipping to
// flat white when ambient + diffuse + specular (+ multiple lights) stack up.
vec3 tonemap(vec3 c) {
    c = c / (c + vec3(1.0));
    return pow(c, vec3(1.0 / 2.2));
}

// THE lighting model. Given a visible surface point, return its linear (pre-
// tonemap) radiance: sky ambient (added once) + the sum over every scene light
// of its Blinn-Phong direct term, each gated by its OWN BVH-traced hard shadow
// ray. Used unchanged by both render modes.
// Ng = geometric (face) normal, used for GI hemisphere + ray offset so samples
// never dip below the real surface (the cause of GI self-occlusion acne when the
// smooth/interpolated N differs from the face). N is still used for shading.
// Direct Blinn-Phong lighting (all lights + their shadow rays), no ambient/GI.
// Reused by shadeSurface and by GI bounce surfaces (avoids GI recursion).
vec3 directLight(vec3 P, vec3 N, vec3 albedo) {
    vec3 Vv = normalize(uEye - P);
    vec3 lit = vec3(0.0);
    for (int i = 0; i < uNumLights; ++i) {
        vec3  toL  = uLightPosArr[i] - P;
        float dist = length(toL);
        if (dist < 1e-5) continue;
        vec3  L     = toL / dist;
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;

        // Slope/distance-scaled origin bias removes self-intersection acne; soft
        // penumbra when uShadowSamples > 1 (jittered rays toward a disk).
        float sEps = (3e-3 + 1.5e-3 * length(P - uEye)) / max(NdotL, 0.2);
        vec3 ro = P + N * sEps;
        float shadow;
        if (uShadowSamples <= 1) {
            shadow = occluded(ro, L, dist) ? 0.0 : 1.0;
        } else {
            vec3 st, sb; _giBasis(L, st, sb);
            int blocked = 0;
            for (int s = 0; s < uShadowSamples; ++s) {
                float r1 = _giHash(gl_FragCoord.xy + vec2(float(s) * 5.3 + float(i), 1.1));
                float r2 = _giHash(gl_FragCoord.yx + vec2(float(s) * 2.9, float(i) * 4.7));
                float rr = uShadowSoftness * sqrt(r1); float ph = 6.2831853 * r2;
                vec3 Lj = normalize(L + (st * cos(ph) + sb * sin(ph)) * rr);
                if (occluded(ro, Lj, dist)) ++blocked;
            }
            shadow = 1.0 - float(blocked) / float(uShadowSamples);
        }
        if (shadow <= 0.0) continue;

        vec3  H     = normalize(L + Vv);
        float NdotH = max(dot(N, H), 0.0);
        vec3  diffuse = albedo * NdotL;
        vec3  spec    = uKs * pow(NdotH, max(uShininess, 1.0));
        lit += (diffuse + spec) * uLightColorArr[i] * shadow;
    }
    return lit;
}

// THE lighting model: ambient/GI (hemisphere environment + AO, optionally with
// uGIBounces color-bleed bounces) + direct light. Ng = geometric (face) normal,
// used for the GI hemisphere + ray offset so samples never dip below the surface
// (the cause of GI self-occlusion acne). N is used for shading.
vec3 shadeSurface(vec3 P, vec3 N, vec3 albedo, vec3 Ng) {
    if (dot(Ng, N) < 0.0) Ng = -Ng;       // orient to the shading hemisphere

    vec3 ambient;
    if (uGISamples > 0) {
        float eps = 2e-3 + 1e-3 * length(P - uEye);
        vec3  ro  = P + Ng * eps;
        vec3 gi = vec3(0.0);
        for (int i = 0; i < uGISamples; ++i) {
            float u1 = _giHash(gl_FragCoord.xy + vec2(float(i) * 1.7, float(i) * 3.1));
            float u2 = _giHash(gl_FragCoord.yx + vec2(float(i) * 2.3, float(i) * 0.7));
            vec3  d  = _giCosHemi(Ng, u1, u2);        // hemisphere about the FACE normal
            if (dot(d, Ng) <= 0.0) continue;          // never sample below the surface
            gi += giSampleRadiance(ro, d);            // sky, or bounced surface radiance
        }
        ambient = albedo * (gi / float(uGISamples)) * uEnvTint;
    } else {
        // Matte ambient: soft gradient only (HDRI lights matte surfaces only via
        // GI / mirror, and shows in the background).
        ambient = albedo * gradientSky(N) * 0.5;
    }
    return ambient + directLight(P, N, albedo);
}
)GLSL";
