# Hybrid Renderer Quality Correction Design

**Status:** Approved in chat on 2026-09-15
**Implementation branch:** `codex/hardware-raster-ray-effects`
**Baseline:** OpenGL 3.3 fragment backend, with the OpenGL 4.3 compute backend remaining an optional acceleration path

## Purpose

Correct the visible quality and consistency defects in the existing hardware-raster plus secondary-ray renderer without changing its Blinn-Phong material model to PBR. The result must use one lighting contract in raster and ray-hit code, apply ray-traced shadows only to direct light, converge stochastic effects over time, reconstruct noisy effects without blurring geometry boundaries, present through an HDR tone-mapping path, sample material textures correctly at distance, honor SSAA in both Editor and Game, and add textbook clear dielectric glass using Unreal-familiar authoring names.

This is a renderer-correction milestone. PBR, rough reflections or frosted glass, arbitrary recursive ray tracing, nested dielectric stacks, caustics, editor-shell redesign, and new content-authoring workflows are separate milestones.

## Problems Being Corrected

1. Raster point lights do not attenuate with distance, while secondary ray hits use inverse-square attenuation.
2. Soft shadows default to one sample per light and therefore produce binary, high-variance results.
3. GI uses only the current frame's samples.
4. No temporal accumulation or edge-aware denoising exists.
5. The composite clamps linear color and applies gamma without exposure or tone mapping.
6. Material textures lack mipmaps and anisotropic filtering.
7. `FRenderQuality::ssaa` is not loaded or applied by the standalone Game path.
8. A single visibility value multiplies the complete raster result, incorrectly darkening ambient, emissive, and specular contributions from unrelated lights.
9. Mirror reflection is added on top of local lighting, so a material with a mirror factor of one is not a true mirror.
10. The material model has no distinct translucent blend mode, physically meaningful refraction, Fresnel reflection, or distance-based absorption for clear glass.

## Rendering Architecture

The corrected frame is evaluated in six logical stages:

```text
Hardware geometry at internal resolution
    -> G-buffer
    -> raster ambient and unshadowed direct-light terms
    -> raw ray effects: shadowed direct light, GI, perfect reflection, clear-glass transmission
    -> temporal accumulation and edge-aware reconstruction
    -> linear HDR hybrid composition
    -> output-resolution resolve, exposure, ACES tone map, linear-to-sRGB
```

`internalResolution = outputResolution * ssaa`. All scene-space passes run at the internal resolution. The final presentation pass downsamples the linear HDR result and performs the display transform exactly once.

The OpenGL 3.3 and OpenGL 4.3 ray backends must produce identical logical outputs. Backend-specific code may differ only in how work is submitted: fullscreen fragment draw versus compute dispatch.

`FRenderFeatures` gains `rayTracedTranslucency`, serialized as `RayTracedTranslucency` and defaulting to true whenever the ray-tracing master feature is enabled. It is a fourth optional secondary effect beside shadows, GI, and reflections. A scene with only ray-traced translucency enabled still schedules the ray-effects pass. The Editor and Project Settings label is `Ray-Traced Translucency`.

## Shared Lighting Contract

Raster Phong lighting and secondary ray-hit lighting must use a shared GLSL source fragment defining point-light evaluation. A point-light sample contains its normalized light direction, distance, attenuation, and radiance. The contract is:

```text
distanceSquared = max(dot(toLight, toLight), 0.01)
attenuation     = 1 / distanceSquared
radiance        = lightColor * lightIntensity * attenuation
diffuse         = albedo * radiance * max(dot(N, L), 0)
specular        = specularColor * radiance
                  * pow(max(dot(N, normalize(L + V)), 0), max(shininess, 1))
direct          = diffuse + specular
```

The `0.01` lower bound corresponds to a minimum light distance of `0.1` world units and prevents singular highlights. Point lights use this contract everywhere, including reflection-hit and GI-hit shading. Directional lights, when added in a later milestone, will not use distance attenuation.

`FRenderScene` continues to submit `lightColor * lightIntensity` as the unattenuated source value. The renderer must name this value consistently and must not interpret it as already distance-attenuated radiance.

## Raster Lighting Outputs

`FRasterLightingOutput` is split into distinct linear HDR meanings:

- ambient radiance;
- unshadowed direct radiance;
- the existing G-buffer emissive term, consumed separately by composition.

Phong direct radiance is evaluated per pixel using the shared point-light contract. Flat and Gouraud retain their defining raster interpolation behavior: their precomputed lighting payload becomes precomputed direct light, while ambient and emissive remain separate. Flat ambient uses the geometric normal; Gouraud ambient uses its interpolated shading normal.

The split is mandatory because a shadow may attenuate direct light only. No composite operation may multiply ambient, emissive, GI, or reflection by shadow visibility.

## Per-Light Soft Shadows

The ray pass no longer returns one scene-wide scalar shadow visibility. It returns `shadowedDirectRadiance`, accumulated per light:

```text
shadowedDirectRadiance = sum(visibility[i] * directContribution[i])
```

Each retained point light receives its own independent soft-shadow sample set. A sample traces toward a disk-jittered direction around that light and contributes zero or one visibility. The default is four shadow samples per light per pixel per frame. The allowed range remains 1 through 16.

For Phong, `directContribution[i]` is the exact result from the shared point-light function. Flat and Gouraud preserve their precomputed direct color by applying a per-channel, contribution-weighted visibility ratio derived from the same per-light samples; they must not fall back to an equal-weight average across lights.

When ray shadows are disabled or the ray backend is unavailable, composition uses raster `unshadowedDirectRadiance`. When ray shadows succeed, it uses reconstructed `shadowedDirectRadiance` instead. Optional-ray failure must never remove raster direct light.

## GI Sampling

GI remains diffuse, hemisphere-sampled indirect lighting under the current Blinn-Phong material model. It is not presented as a physically complete path tracer.

- Default per-frame samples: 4.
- Allowed per-frame range: 0 through 32.
- Bounce range: 0 through 4, unchanged.
- Each frame uses a different low-discrepancy/hash sequence selected by `frameIndex`.
- Secondary direct lighting uses the shared point-light contract, including inverse-square attenuation and Blinn specular where the hit material supplies it.

The lower default per-frame count is intentional: temporal convergence and reconstruction provide stability without multiplying the real-time ray cost. GI strength remains an artistic multiplier applied in linear space.

## Temporal Accumulation

The first implementation uses reset-on-change accumulation rather than motion-vector reprojection. This avoids ghost trails while providing convergence for static views.

The GL 3.3-compatible reconstruction stage owns ping-pong history textures and is shared by both ray backends. It accumulates shadowed direct radiance and GI with a running average capped at 32 history frames. Perfect mirror reflection is deterministic and is not spatially blurred; it may reuse history only while the full history signature remains unchanged.

The history signature includes:

- output and internal dimensions;
- camera view and projection state;
- ray-scene geometry, instance-transform, material, and texture revisions;
- point-light positions and source values;
- environment texture and environment-light settings;
- enabled render features;
- shadow, GI, reflection, exposure, and SSAA quality settings;
- selected ray backend and OpenGL context generation.

Any signature change clears the affected histories before the new frame contributes. Backend failure, fallback, shutdown, and context recreation also clear history. A static signature increments `frameIndex`; a reset starts again at zero.

## Edge-Aware Denoising

After temporal accumulation:

- shadowed direct radiance receives two separable edge-aware bilateral passes;
- GI receives three A-trous passes with increasing step widths 1, 2, and 4;
- perfect mirror reflection receives no spatial blur.

Depth, geometric normal, and object/material identity guide the filters. Samples across an object boundary, a large depth discontinuity, or a strongly different normal receive zero or sharply reduced weight. Denoising operates on linear HDR values and may not clamp them to display range.

The denoiser owns no scene data and depends only on ray-effect textures plus the current G-buffer. All textures and framebuffers are allocated on size or context changes, not per frame.

## Unreal-Familiar Clear-Glass Material Inputs

Clear glass uses a separate translucent material contract rather than overloading `mirrorFactor`. The authoring names intentionally follow Unreal's familiar legacy and Substrate vocabulary while retaining a small fixed material model:

```text
Blend Mode
    Opaque
    Translucent
Opacity
Refraction
Transmittance Color
Transmittance Distance
Cast Ray Traced Shadows
```

The persisted runtime properties are:

```cpp
enum class EMaterialBlendMode
{
    Opaque,
    Translucent,
};

EMaterialBlendMode blendMode = EMaterialBlendMode::Opaque;
float opacity = 1.0f;
float refraction = 1.52f;
glm::vec3 transmittanceColor = glm::vec3(1.0f);
float transmittanceDistance = 1.0f;
bool castRayTracedShadows = true;
```

`Opacity` is clamped to `[0, 1]`; zero means fully transmissive and one means fully locally shaded. `Refraction` is the material's index of refraction and is clamped to `[1.0, 2.42]`. `1.33` represents water, `1.52` ordinary glass, and `2.42` diamond. Opaque materials ignore all transmission fields.

`Transmittance Color` is the fraction of each color channel remaining after travelling `Transmittance Distance` world units through the medium. The editor exposes those intuitive values and derives the absorption coefficient used by the renderer:

```text
sigmaA(channel) = -log(clamp(transmittanceColor(channel), 0.0001, 1))
                  / max(transmittanceDistance, 0.0001)
beer(distance)  = exp(-sigmaA * distance)
```

This is equivalent to converting an authored transmittance at a reference thickness into a color-channel mean free path. White produces no colored absorption. The serialized values remain the author-facing color and reference distance so existing material files are readable without an opaque derived representation.

`Masked` blend mode, alpha sorting, and general alpha compositing are not part of this milestone. `Translucent` specifically means closed-volume, ray-refractive dielectric material; it is not a screen-space alpha blend.

## Clear-Glass Ray Transport

For a visible translucent surface, the renderer performs a textbook dielectric traversal:

```text
camera ray hits air-to-glass boundary
    -> compute Schlick Fresnel and Snell refraction
    -> trace inside the same closed mesh to its exit boundary
    -> measure inside distance and apply Beer-Lambert absorption
    -> compute glass-to-air refraction
    -> trace the continuation ray to the scene or environment
```

Normals are oriented against the incident direction and the ratio `eta = n1 / n2` is chosen for entry or exit. Schlick Fresnel uses:

```text
F0 = ((n1 - n2) / (n1 + n2))^2
F  = F0 + (1 - F0) * (1 - cosTheta)^5
```

If Snell refraction has no real solution, total internal reflection selects the reflected ray with weight one. A successful refraction supports exactly one entry boundary, one exit boundary on the same closed mesh, and one continuation hit or environment miss. The continuation hit is shaded with the shared point-light contract. It does not recursively open another dielectric or mirror traversal.

Malformed or open geometry for which no matching exit is found uses reflected radiance for that sample and emits one deduplicated material/mesh diagnostic rather than leaking an invalid ray or returning stale data.

When `Cast Ray Traced Shadows` is enabled, shadow traversal through this material multiplies the light by its Fresnel transmission and Beer-Lambert attenuation for the measured interior segment. It does not bend the shadow ray and therefore produces colored transmission but not caustics. When the flag is disabled, the material is skipped by ray-shadow occlusion. Opaque materials preserve binary occlusion.

## Mirror and Glass Composition

Mirror remains the existing scalar material control `mirrorFactor`, clamped to `[0, 1]`. It is an engine-specific legacy control retained for project compatibility; a later PBR milestone will migrate authored mirrors toward Unreal-style `Metallic = 1` and `Roughness = 0`.

Composition first reserves energy for translucent transport, then divides the remaining opaque energy between local light and the legacy mirror lobe:

```text
kt       = blendMode == Translucent ? 1 - clamp(opacity, 0, 1) : 0
kr       = (1 - kt) * clamp(mirrorFactor * reflectionStrength, 0, 1)
kl       = 1 - kt - kr
local    = ambient + selectedDirect + GI
glass    = Fresnel * reflectedRadiance
           + (1 - Fresnel) * beer(insideDistance) * refractedRadiance
surface  = emissive + kl * local + kr * reflectedRadiance + kt * glass
```

Consequences:

- Opaque `kr = 0` is the ordinary local Blinn-Phong result.
- Opaque `kr = 1` removes local diffuse/specular/GI and produces a perfect mirror plus emissive.
- Translucent `opacity = 0` produces Fresnel-weighted reflection and refraction with no local diffuse/specular/GI.
- Shadowing affects only `selectedDirect`; it never darkens reflection or emissive.
- The reflection ray uses `reflect(viewRay, shadingNormal)` and shades one secondary hit using the shared lighting contract, or returns the environment on a miss.

This milestone supports one perfect reflection ray and one dielectric entry/exit traversal. Recursive mirrors, metallic response, roughness-filtered reflections, frosted transmission, nested dielectric media, and caustics belong to the later PBR milestone.

If ray reflections are disabled, effective `kr` is zero. If ray-traced translucency is disabled, effective `kt` is zero. If the ray backend is unavailable, both are zero so the material remains locally visible instead of turning black or disappearing.

## Linear HDR Composition and Presentation

Hybrid composition writes an `RGBA16F` linear HDR target and performs no display-range clamp:

```text
hybridHDR = emissive + kl * (ambient + selectedDirect + GI)
            + kr * reflection + kt * glass
```

The presentation pass then performs:

```text
resolvedHDR = linear-filtered internal HDR at output resolution
exposed     = resolvedHDR * exp2(exposureEV)
mapped      = ACES fitted(exposed)
display     = linearToSRGB(mapped)
```

`exposureEV` defaults to `0.0` and is stored in both Editor and Game render-quality profiles. The ACES fitted curve is the engine's only tone mapper for this milestone. The presentation pass is the only location that converts linear values to display sRGB; neither raster lighting nor ray effects applies gamma.

Depth visualization bypasses lighting, accumulation, denoising, and ACES, and emits its existing normalized diagnostic image.

## Material Texture Sampling

Base-color textures are stored and sampled as sRGB data that decodes to linear values exactly once. Numeric textures and HDR environments remain linear.

Every raster material texture receives a complete mip chain and uses trilinear minification. When `GL_EXT_texture_filter_anisotropic` is available, the sampler uses the minimum of `8x` and the implementation's reported maximum. Without the extension, the renderer silently retains trilinear filtering.

Ray material-array layers must cover the complete normalized UV domain before mip generation. Each source image is resampled into its full array layer, preventing higher mip levels from blending with unused black texels. The ray shaders then sample normalized layer UV directly. Repeat and clamp behavior remains material-controlled.

Texture upload, mip generation, and sampler configuration happen only when the material texture revision changes. Existing upload rollback and diagnostic behavior remains intact.

## Shared SSAA Behavior

`FRenderQuality::ssaa` remains an integer with supported values `1` and `2`. Both Editor and Game load it from their respective render-quality profiles and pass it through the same render-target sizing logic.

- `1`: internal size equals output size.
- `2`: internal width and height are doubled, producing four times as many internal pixels.

Game renders to an internal offscreen HDR target whenever SSAA is greater than one, then resolves through the common presentation pass to the window backbuffer. Editor and Game must not implement separate filtering or tone-mapping formulas. Resizing the window or changing SSAA recreates size-dependent resources and resets temporal history.

The Game default remains SSAA 1 to avoid imposing a fourfold pixel cost on packaged assignments.

## Configuration Defaults

The corrected defaults are:

```text
SSAA              = 1
ShadowSamples     = 4
ShadowSoftness    = 0.05
GISamples         = 4
GIBounces         = 1
GIStrength        = 1.0
ReflectionStrength= 1.0
ExposureEV        = 0.0
TemporalFrames    = 32
Anisotropy        = 8
```

Input values are clamped at the render-quality boundary before reaching shaders. Old project files without new keys receive these defaults. Existing keys retain their serialized names.

## Failure and Fallback Rules

- A ray backend failure produces neutral optional effects and preserves the complete raster result.
- An invalid translucent closed volume falls back to its Fresnel reflection sample and reports one deduplicated diagnostic.
- A reconstruction failure discards its histories and uses the current raw ray outputs for that frame when those outputs are valid.
- An HDR-composite or presentation-pass failure reports a renderer diagnostic and returns failure through the existing render-executor contract; it must not present stale textures.
- Allocation failures roll back newly created GL objects and leave previously valid resources usable when context-compatible.
- Unsupported anisotropy is a capability fallback, not an error.
- All new fullscreen stages preserve the caller's OpenGL state using the existing state-guard discipline.

## Verification Strategy

### Contract and Unit Tests

- Verify raster and both ray shaders consume the same point-light GLSL function.
- Verify inverse-square attenuation values at distances 1, 2, and 4.
- Verify shadowed direct accumulation is per light and no longer exports or consumes a global visibility multiplier.
- Verify mirror composition at `kr` values 0, 0.5, and 1.
- Verify material blend-mode defaults and serialization of `Opacity`, `Refraction`, `TransmittanceColor`, `TransmittanceDistance`, and `CastRayTracedShadows`.
- Verify Schlick Fresnel at normal and grazing incidence, Snell refraction for air/glass transitions, total internal reflection, and Beer-Lambert attenuation at zero, reference, and doubled reference distance.
- Verify the ACES implementation against fixed linear HDR inputs and verify linear-to-sRGB occurs once.
- Verify history signatures remain stable for unchanged frames and change for every listed invalidation input.
- Verify configuration clamping and backward-compatible defaults.

### OpenGL Integration Tests

- Compile and execute the GL 3.3 fragment shaders on the minimum path.
- Compile and execute the GL 4.3 compute shaders when supported.
- Confirm all accumulation and denoising resources are allocated at warm-up or resize, with no steady-state per-frame allocation.
- Confirm texture mip levels are complete and anisotropy never exceeds the reported capability.
- Confirm Game SSAA 2 renders at twice the window width and height and presents at the actual window size.
- Confirm renderer state is restored after every new fullscreen pass.

### Visual Acceptance Scenes

- Two differently colored point lights at different distances: raster surfaces and reflection/GI hits show matching attenuation and color response.
- A lit object with strong ambient and emissive terms: ray shadows remove only occluded direct light.
- A static soft-shadow scene: noise visibly converges over successive frames without crossing depth or normal edges.
- A static GI scene: the accumulated image stabilizes, and moving the camera clears history immediately with no ghost trail.
- A mirror cube with `mirrorFactor = 1`: no local diffuse or Blinn highlight remains, and the reflected scene or environment supplies its visible color.
- A closed clear-glass cube with `Opacity = 0` and `Refraction = 1.52`: its background is refracted through both boundaries, grazing angles become more reflective, and colored transmittance increases with interior distance.
- The same clear-glass cube in a shadow path: `Cast Ray Traced Shadows` selects colored transmissive shadowing when on and no ray-shadow occlusion when off.
- A high-frequency texture viewed obliquely: distant shimmer is reduced by mipmapping and anisotropic filtering.
- The same scene in Editor and Game at SSAA 1 and 2: output sizing and presentation behavior match.

## Acceptance Criteria

The milestone is complete when all automated renderer suites pass on the GL 3.3 baseline, optional GL 4.3 tests pass on capable hardware, the visual acceptance scenes satisfy the behaviors above, and the following invariants hold:

- raster and ray-hit point lighting cannot diverge in attenuation or Blinn-Phong terms;
- shadows affect direct light per light and never multiply the complete raster result;
- static stochastic effects converge while any relevant state change resets history;
- denoising does not blend across object identities or strong geometric edges;
- HDR values remain unclamped until ACES presentation;
- a full mirror replaces local lighting instead of adding to it;
- translucent glass uses Fresnel-weighted reflection, two-boundary Snell refraction, total-internal-reflection fallback, and Beer-Lambert absorption without double-counting local light;
- material textures use complete mip chains and capability-bounded anisotropy;
- Editor and Game use the same SSAA and presentation path;
- no size-stable frame allocates renderer textures or framebuffers after warm-up.
