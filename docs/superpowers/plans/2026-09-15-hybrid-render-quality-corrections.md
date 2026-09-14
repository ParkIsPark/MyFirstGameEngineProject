# Hybrid Renderer Quality Corrections Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the hardware-raster plus secondary-ray renderer produce consistent Blinn-Phong lighting, stable soft shadows and GI, energy-bounded mirror and clear-glass transport, HDR tone-mapped output, correctly filtered textures, and matching Editor/Game SSAA behavior.

**Architecture:** Keep `UWorldRenderer` as the only normal Editor/Game entry point. Split raster lighting into ambient and unshadowed-direct HDR outputs, make both ray backends return per-light shadowed direct radiance plus optical contributions, reconstruct stochastic outputs through one GL 3.3-compatible temporal/denoise stage, then compose to an internal `RGBA16F` target and present through one resolution-aware ACES pass. Material authoring remains Blinn-Phong but gains Unreal-familiar translucent fields and a bounded textbook dielectric traversal.

**Tech Stack:** C++17, OpenGL 3.3 core minimum, optional OpenGL 4.3 Compute/SSBO, GLSL 3.30/4.30, GLEW, GLFW, GLM, Dear ImGui, existing `FArchive`/`FIniFile`, Visual Studio 2022 Win32 builds, and standalone/integrated renderer tests.

**Spec:** `docs/superpowers/specs/2026-09-15-hybrid-render-quality-correction-design.md`

## Global Constraints

- OpenGL 3.3 remains the minimum supported runtime; OpenGL 4.3 Compute remains an optional acceleration backend with the same logical output contract.
- Preserve Flat, Gouraud, and Phong raster shading. Phong and all secondary-hit direct lighting use one shared Blinn-Phong point-light definition.
- Point-light attenuation is `1 / max(distanceSquared, 0.01)` in every corrected hardware/ray path.
- Supported SSAA values are exactly `1` and `2`; packaged Game defaults to `1`.
- Corrected defaults are `ShadowSamples=4`, `GISamples=4`, `TemporalFrames=32`, `ExposureEV=0.0`, and `Anisotropy=8`.
- Shadows replace only the direct-light term and never multiply ambient, emissive, GI, reflection, or transmission.
- Mirror and clear-glass energy weights must sum to at most one. No PBR, rough reflection, frosted glass, nested dielectric stack, arbitrary recursion, or caustics are introduced.
- Clear glass guarantees one closed-mesh entry, one same-mesh exit, total-internal-reflection fallback, Beer-Lambert absorption, and one continuation hit/environment miss.
- Base-color textures decode sRGB to linear exactly once. HDR/environment and numeric textures remain linear.
- All size/context-dependent GL resources allocate at initialization or resize and perform zero steady-state per-frame texture/framebuffer allocation.
- Every new engine `.h` and `.cpp` must be added to `Engine.vcxproj`, `Test/Test.vcxproj`, `Template/Template.vcxproj`, and the three matching `.filters` files in the same task. Standalone `Test/*.cpp` files with their own `main()` are not added to `Test.vcxproj`.
- Preserve Lua sources, generated-project behavior, deprecated renderer isolation, and the existing untracked `Test/Config/` directory.

## Verification Command Convention

Build the engine before standalone or integrated gates:

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
$env:_CL_ = '/FS'
& $msbuild Engine.sln /m:1 /nr:false /p:Configuration=Debug /p:Platform=Win32 /clp:ErrorsOnly
```

Run one standalone test with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name TestStem
```

Run integrated OpenGL tests with a temporary evidence directory:

```powershell
$renderEvidence = Join-Path ([IO.Path]::GetTempPath()) ('hybrid-quality-' + [Guid]::NewGuid())
New-Item -ItemType Directory -Path $renderEvidence | Out-Null
& .\bin\Test.exe "--raster-lighting-selftest=$renderEvidence\lighting.ppm"
& .\bin\Test.exe "--ray-effects-selftest=$renderEvidence\rays.ppm"
```

---

## Task 1: Freeze Render-Quality Values and Reference Math

**Files:**

- Create: `Engine/Render/FRenderMath.h`
- Create: `Engine/Render/FRenderMath.cpp`
- Create: `Engine/Render/FRenderQualitySettings.h`
- Create: `Engine/Render/FRenderQualitySettings.cpp`
- Create: `Test/RenderQualityMathTest.cpp`
- Modify: `Engine/Render/FRenderQuality.h`
- Modify: all six Visual Studio project/filter files listed in Global Constraints

**Interfaces:**

```cpp
struct FRenderQuality
{
    int ssaa = 1;
    float ambientStrength = 1.0f;
    int giSamples = 4;
    int giBounces = 1;
    float giStrength = 1.0f;
    float reflStrength = 1.0f;
    float shininess = 32.0f;
    int shadowSamples = 4;
    float shadowSoftness = 0.05f;
    float exposureEV = 0.0f;
    int temporalFrames = 32;
    float anisotropy = 8.0f;
    bool depthView = false;
};

FRenderQuality SanitizeRenderQuality(FRenderQuality value);
float PointLightAttenuation(float distanceSquared);
float SchlickFresnel(float cosTheta, float n1, float n2);
glm::vec3 BeerLambertFromTransmittance(
    const glm::vec3& transmittanceColor,
    float referenceDistance,
    float travelledDistance);
glm::vec3 ACESFitted(const glm::vec3& linearHDR);

FRenderQuality ReadRenderQuality(
    const FIniFile& ini, const char* section, const FRenderQuality& defaults);
void WriteRenderQuality(
    std::ostream& output, const char* section, const FRenderQuality& quality);
```

- [ ] **Step 1: Write the failing render-quality/math test.**

```cpp
FRenderQuality invalid;
invalid.ssaa = 7;
invalid.shadowSamples = 0;
invalid.giSamples = 99;
invalid.giBounces = -2;
invalid.temporalFrames = 1000;
invalid.anisotropy = -1.0f;
const FRenderQuality q = SanitizeRenderQuality(invalid);
assert(q.ssaa == 2);
assert(q.shadowSamples == 1);
assert(q.giSamples == 32);
assert(q.giBounces == 0);
assert(q.temporalFrames == 32);
assert(q.anisotropy == 1.0f);
assert(Near(PointLightAttenuation(1.0f), 1.0f));
assert(Near(PointLightAttenuation(4.0f), 0.25f));
assert(Near(PointLightAttenuation(16.0f), 0.0625f));
assert(Near(SchlickFresnel(1.0f, 1.0f, 1.5f), 0.04f, 0.001f));
assert(NearVec(BeerLambertFromTransmittance({0.5f, 0.25f, 1.0f}, 2.0f, 2.0f),
               {0.5f, 0.25f, 1.0f}, 0.001f));
assert(ACESFitted({4.0f, 2.0f, 1.0f}).r <= 1.0f);
```

- [ ] **Step 2: Run `RenderQualityMathTest`; expect compilation failure because the new functions and fields do not exist.**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name RenderQualityMathTest
```

- [ ] **Step 3: Add the exact quality fields/defaults and implement clamping in `SanitizeRenderQuality`.** Clamp SSAA to `1` or `2`, shadows to `1..16`, GI samples to `0..32`, GI bounces to `0..4`, temporal frames to `1..32`, anisotropy to `1..16`, reflection strength to `0..1`, and finite exposure to `-16..16`.

- [ ] **Step 4: Implement the reference math without display gamma.** Use the spec's inverse-square, Schlick, Beer-Lambert, and Narkowicz ACES-fitted equations:

```cpp
glm::vec3 ACESFitted(const glm::vec3& x)
{
    constexpr float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return glm::clamp((x * (a * x + b)) / (x * (c * x + d) + e),
                      glm::vec3(0.0f), glm::vec3(1.0f));
}
```

- [ ] **Step 5: Extract INI persistence from `EditorEngine.cpp` into `FRenderQualitySettings`.** Read missing keys from the supplied defaults, call `SanitizeRenderQuality`, and write every non-editor-only field including `SSAA`, `ExposureEV`, `TemporalFrames`, and `Anisotropy`.

- [ ] **Step 6: Re-run `RenderQualityMathTest`; expect exit code 0.**

- [ ] **Step 7: Add the four new engine files to all project/filter files and build `Debug|Win32`; expect zero errors.**

- [ ] **Step 8: Commit the quality contract.**

```powershell
git add Engine/Render/FRenderMath.* Engine/Render/FRenderQuality.h Engine/Render/FRenderQualitySettings.* Test/RenderQualityMathTest.cpp Engine.vcxproj Engine.vcxproj.filters Test/Test.vcxproj Test/Test.vcxproj.filters Template/Template.vcxproj Template/Template.vcxproj.filters
git commit -m "refactor: define renderer quality and reference math"
```

## Task 2: Add Unreal-Familiar Clear-Glass Material and Feature Contracts

**Files:**

- Modify: `Engine/Mesh/Material.h`
- Modify: `Engine/Mesh/Material.cpp`
- Modify: `Engine/Mesh/UMaterial.cpp`
- Modify: `Engine/Render/FRenderFeatures.h`
- Modify: `Engine/Render/FRenderPipelinePlan.cpp`
- Modify: `Engine/Render/FRenderOutputs.h`
- Modify: `Engine/Render/FRenderScene.h`
- Modify: `Engine/Render/FRenderScene.cpp`
- Modify: `Engine/Serialization/FWorldSerializer.cpp`
- Modify: `Engine/Framework/FProjectDescriptor.cpp`
- Modify: `Test/serialize_test.cpp`
- Modify: `Test/RenderPipelinePlanTest.cpp`
- Modify: `Test/RenderSettingsMigrationTest.cpp`
- Create: `Test/MaterialOpticsTest.cpp`

**Interfaces:**

```cpp
enum class EMaterialBlendMode { Opaque, Translucent };

struct Material
{
    EMaterialBlendMode blendMode = EMaterialBlendMode::Opaque;
    float opacity = 1.0f;
    float refraction = 1.52f;
    glm::vec3 transmittanceColor = glm::vec3(1.0f);
    float transmittanceDistance = 1.0f;
    bool castRayTracedShadows = true;
    void SanitizeOptics();
};

struct FRenderFeatures
{
    bool rayTracedTranslucency = true;
};

struct FResolvedRenderMaterial
{
    EMaterialBlendMode blendMode = EMaterialBlendMode::Opaque;
    float opacity = 1.0f;
    float refraction = 1.52f;
    glm::vec3 transmittanceColor = glm::vec3(1.0f);
    float transmittanceDistance = 1.0f;
    bool castRayTracedShadows = true;
};
```

- [ ] **Step 1: Extend `serialize_test.cpp` and add `MaterialOpticsTest.cpp`.** Assert old material text keeps opaque defaults, format-2 save/load round-trips all six properties, invalid blend strings become `Opaque`, opacity clamps to `0..1`, refraction clamps to `1.0..2.42`, transmittance color clamps to `0.0001..1`, and distance clamps to at least `0.0001`.

- [ ] **Step 2: Extend renderer feature tests.** Assert `rayTracedTranslucency` alone schedules `RayTracedEffects`, format-3 worlds and project INI round-trip `RayTracedTranslucency`, and absent fields retain the caller's/default value.

- [ ] **Step 3: Run the three tests; expect failures for missing fields and serialization.**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name MaterialOpticsTest
powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name RenderPipelinePlanTest
powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name RenderSettingsMigrationTest
```

- [ ] **Step 4: Implement human-readable material serialization.** Save `blendMode = Opaque|Translucent`, `opacity`, `refraction`, `transmittanceColor`, `transmittanceDistance`, and `castRayTracedShadows`; write `MaterialFormat = 2` in `UMaterial::Save`; leave missing fields at defaults on load. `Material::SanitizeOptics` performs the exact Task 2 clamps after loading and before a resolved render snapshot is created.

- [ ] **Step 5: Add `rayTracedTranslucency` to pass planning, world format-3 named fields, and project settings parsing.** Legacy all-on modes set it true; no world-format bump is needed because the new named field is optional and format 3 already supports partial defaults.

- [ ] **Step 6: Copy sanitized optical values into `FResolvedRenderMaterial` and `FLogicalGBufferSample`.** Keep `mirrorFactor` for compatibility; do not copy texture bytes into the per-frame scene snapshot.

- [ ] **Step 7: Re-run the three focused tests and the full standalone matrix; expect exit code 0.**

- [ ] **Step 8: Commit the material and feature contracts.**

```powershell
git add Engine/Mesh/Material.* Engine/Mesh/UMaterial.cpp Engine/Render/FRenderFeatures.h Engine/Render/FRenderPipelinePlan.cpp Engine/Render/FRenderOutputs.h Engine/Render/FRenderScene.* Engine/Serialization/FWorldSerializer.cpp Engine/Framework/FProjectDescriptor.cpp Test/serialize_test.cpp Test/MaterialOpticsTest.cpp Test/RenderPipelinePlanTest.cpp Test/RenderSettingsMigrationTest.cpp
git commit -m "feat: define clear glass material inputs"
```

## Task 3: Share Point-Light Math and Split Raster Lighting Terms

**Files:**

- Create: `Engine/Render/Shaders/SharedLightingShaderSource.h`
- Create: `Test/HybridLightingContractTest.cpp`
- Modify: `Engine/Render/Shaders/HardwareRasterShaders.h`
- Modify: `Engine/Render/Shaders/RasterLightingShaders.h`
- Modify: `Engine/Render/UHardwareRasterizer.cpp`
- Modify: `Engine/Render/URasterLightingPass.h`
- Modify: `Engine/Render/URasterLightingPass.cpp`
- Modify: `Engine/Render/FRenderOutputs.h`
- Modify: `Engine/Render/FRenderScene.h`
- Modify: `Engine/Render/FRenderScene.cpp`
- Modify: `Test/main.cpp`
- Modify: all six Visual Studio project/filter files for the new header

**Interfaces:**

```cpp
struct FRasterLightingOutput
{
    bool valid = false;
    FRenderOutputView environmentAmbientTarget;
    FRenderOutputView unshadowedDirectTarget;
};

namespace SharedLightingShaderSource
{
    std::string PointLightFunctions();
    std::string BuildHardwareGeometryShader();
    std::string BuildRasterLightingFragmentShader();
}
```

The shared GLSL function body is:

```glsl
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
```

- [ ] **Step 1: Write `HybridLightingContractTest.cpp`.** Assert the shared source contains one `evaluatePointLight` definition, both assembled raster shaders contain it exactly once, `FRenderPointLight` exposes `sourceIntensity` instead of ambiguous `radiance`, and the CPU reference gives 1:1/4:1/16 energy at distances 1, 2, and 4.

- [ ] **Step 2: Extend `--raster-lighting-selftest` with distance probes and ambient/direct separation probes.** Use a black-ambient, diffuse-only point at distances 1, 2, and 4; separately use zero direct light with nonzero ambient and emissive.

- [ ] **Step 3: Run the contract test and raster self-test; expect failures because attenuation and split targets do not exist.**

- [ ] **Step 4: Add the shared GLSL source assembler and replace duplicate point-light formulas in hardware Flat/Gouraud precomputation and raster Phong lighting.** Rename `FRenderPointLight::radiance` to `sourceIntensity` at all normal/deprecated consumers so attenuation is never implied to be pre-applied.

- [ ] **Step 5: Change Flat/Gouraud precomputed RGB to direct light only.** Flat uses the face center/geometric normal; Gouraud stores vertex-evaluated direct light. Ambient is evaluated in `RasterLightingShaders` from geometric normal for Flat and interpolated shading normal for Gouraud; emissive remains the existing G-buffer semantic.

- [ ] **Step 6: Change `URasterLightingPass` to two `RGBA16F` attachments.** Draw `environmentAmbientTarget` and `unshadowedDirectTarget` in one MRT fullscreen pass; uncovered pixels store sky only in the first target and zero direct in the second.

- [ ] **Step 7: Update allocation rollback, ownership counts, state restoration, and `FRasterLightingOutput` validation for both textures.** No `Composite` behavior changes in this task beyond temporarily summing ambient, direct, and emissive to preserve visible output.

- [ ] **Step 8: Re-run the contract and raster self-tests.** Require distance ratios within readback tolerance, unchanged Flat/Gouraud/Phong distinction, and ambient/emissive unaffected by zero direct.

- [ ] **Step 9: Commit shared light evaluation and split raster output.**

```powershell
git add Engine/Render/Shaders/SharedLightingShaderSource.h Engine/Render/Shaders/HardwareRasterShaders.h Engine/Render/Shaders/RasterLightingShaders.h Engine/Render/UHardwareRasterizer.cpp Engine/Render/URasterLightingPass.* Engine/Render/FRenderOutputs.h Engine/Render/FRenderScene.* Test/HybridLightingContractTest.cpp Test/main.cpp Engine.vcxproj Engine.vcxproj.filters Test/Test.vcxproj Test/Test.vcxproj.filters Template/Template.vcxproj Template/Template.vcxproj.filters
git commit -m "fix: unify raster and ray point lighting"
```

## Task 4: Replace Global Shadow Visibility with Shadowed Direct Radiance

**Files:**

- Modify: `Engine/Render/FRenderOutputs.h`
- Modify: `Engine/Render/IRayTracingBackend.h`
- Modify: `Engine/Render/UGL33RayTracingBackend.h`
- Modify: `Engine/Render/UGL33RayTracingBackend.cpp`
- Modify: `Engine/Render/UGL43RayTracingBackend.h`
- Modify: `Engine/Render/UGL43RayTracingBackend.cpp`
- Modify: `Engine/Render/Shaders/SharedLightingShaderSource.h`
- Modify: `Engine/Render/Shaders/RayEffectsFragmentShaders.h`
- Modify: `Engine/Render/Shaders/RayEffectsComputeShaders.h`
- Modify: `Engine/Render/Shaders/RasterLightingShaders.h`
- Modify: `Engine/Render/URasterLightingPass.cpp`
- Modify: `Test/RayEffectsSchedulingTest.cpp`
- Modify: `Test/main.cpp`

**Output contract:**

```cpp
struct FRayEffectOutputs
{
    std::optional<FRenderOutputView> shadowedDirectTarget;
    std::optional<FRenderOutputView> globalIlluminationTarget;
    std::optional<FRenderOutputView> reflectionTarget;
};
```

The shadow attachment is `RGBA16F`; RGB is shadowed direct radiance and alpha is one. No global visibility scalar remains in normal hardware output or composite uniforms.

- [ ] **Step 1: Update `RayEffectsSchedulingTest` fake outputs and add an assertion that failed/disabled shadows leave `shadowedDirectTarget` empty.**

- [ ] **Step 2: Extend `--ray-effects-selftest` with two differently colored point lights.** Arrange an occluder to block only one light and assert the blocked light's color channel is removed while the unblocked light remains.

- [ ] **Step 3: Run the scheduler and ray-effects tests; expect failure while `shadowVisibilityTarget` and one averaged scalar still exist.**

- [ ] **Step 4: Add `BuildRayEffectsFragmentShader` and `BuildRayEffectsComputeShader` to `SharedLightingShaderSource`, then make both assembled ray shaders evaluate each light independently.** For Phong pixels, accumulate `visibility[i] * (diffuse[i] + specular[i])` using the shared point-light source. Seed each of the default four shadow rays with pixel, light, and sample indices; Task 7 extends this stable seed with the temporal frame index.

- [ ] **Step 5: Preserve Flat/Gouraud interpolation.** Compute per-channel `shadowRatio = shadowedEstimated / max(unshadowedEstimated, 1e-5)` from the per-light samples and output `precomputedDirect * clamp(shadowRatio, 0, 1)`; use one for channels whose denominator is zero.

- [ ] **Step 6: Allocate `RGBA16F` shadowed-direct textures in both backends and publish the new logical target.** Disabled shadows allocate no shadow attachment; failed shadows publish no target. Replace the GI-hit `direct()` helper at the same time so its diffuse and Blinn specular terms also call the shared function with the packed hit material's specular color and shininess.

- [ ] **Step 7: Change temporary composite behavior to select ray `shadowedDirectTarget` when valid and raster `unshadowedDirectTarget` otherwise.** Sum ambient and emissive separately, then preserve the existing GI and reflection additions until Task 5 replaces reflection addition with energy-bounded optical composition. Delete `uShadowVisibility` and `uHasShadowVisibility`.

- [ ] **Step 8: Re-run scheduler, GL3.3 ray effects, optional GL4.3 comparison, and raster-only fallback tests; expect all to pass.**

- [ ] **Step 9: Commit per-light shadowed direct output.**

```powershell
git add Engine/Render/FRenderOutputs.h Engine/Render/IRayTracingBackend.h Engine/Render/UGL33RayTracingBackend.* Engine/Render/UGL43RayTracingBackend.* Engine/Render/Shaders/SharedLightingShaderSource.h Engine/Render/Shaders/RayEffectsFragmentShaders.h Engine/Render/Shaders/RayEffectsComputeShaders.h Engine/Render/Shaders/RasterLightingShaders.h Engine/Render/URasterLightingPass.cpp Test/RayEffectsSchedulingTest.cpp Test/main.cpp
git commit -m "fix: shadow direct light per source"
```

## Task 5: Implement Energy-Bounded Mirror and Textbook Clear Glass

**Files:**

- Modify: `Engine/Render/FRenderScene.h`
- Modify: `Engine/Render/FRenderScene.cpp`
- Modify: `Engine/Render/FRenderMath.h`
- Modify: `Engine/Render/FRenderMath.cpp`
- Modify: `Engine/RayTracing/FRaySceneCache.h`
- Modify: `Engine/RayTracing/FRaySceneCache.cpp`
- Modify: `Engine/Render/FRenderOutputs.h`
- Modify: `Engine/Render/IRayTracingBackend.h`
- Modify: `Engine/Render/IRayTracingBackend.cpp`
- Modify: `Engine/Render/UGL33RayTracingBackend.h`
- Modify: `Engine/Render/UGL33RayTracingBackend.cpp`
- Modify: `Engine/Render/UGL43RayTracingBackend.h`
- Modify: `Engine/Render/UGL43RayTracingBackend.cpp`
- Modify: `Engine/Render/Shaders/RayEffectsFragmentShaders.h`
- Modify: `Engine/Render/Shaders/RayEffectsComputeShaders.h`
- Modify: `Engine/Render/Shaders/RasterLightingShaders.h`
- Modify: `Engine/Render/URasterLightingPass.cpp`
- Modify: `Test/WorldRendererRoutingTest.cpp`
- Modify: `Test/RayEffectsSchedulingTest.cpp`
- Modify: `Test/main.cpp`

**Packed material additions:**

```cpp
struct FPackedRayScene
{
    std::vector<glm::vec4> materialTexels;        // eight texels per hit material
    std::vector<glm::vec4> primaryOpticsTexels;   // two texels per dense material identity
    std::vector<std::string> warnings;             // deterministic closed-volume diagnostics
};

struct FMaterialOpticalWeights
{
    float local = 1.0f;
    float mirror = 0.0f;
    float transmission = 0.0f;
};

FMaterialOpticalWeights ResolveMaterialOpticalWeights(
    bool translucent, float opacity, float mirrorFactor,
    float reflectionStrength, bool rayEffectsAvailable);

bool IsClosedTriangleMesh(const UMesh& mesh);
```

The two primary optics texels are fixed as:

```text
0 = transmittanceColor.rgb, opacity
1 = refraction, transmittanceDistance, blendMode(0/1), castRayTracedShadows(0/1)
```

Task 5 replaces `reflectionTarget` with `opticalContributionTarget`. The optical output stores:

```cpp
struct FRayEffectOutputs
{
    std::optional<FRenderOutputView> shadowedDirectTarget;
    std::optional<FRenderOutputView> globalIlluminationTarget;
    std::optional<FRenderOutputView> opticalContributionTarget;
};
```

```text
opticalContribution.rgb = kr * reflected + kt * glass
opticalContribution.a   = kl
kt = translucent ? 1 - opacity : 0
kr = (1 - kt) * clamp(mirrorFactor * reflectionStrength, 0, 1)
kl = 1 - kt - kr
```

- [ ] **Step 1: Extend `MaterialOpticsTest` with exact energy-weight assertions.** Test `ResolveMaterialOpticalWeights` for ordinary local material, half mirror, full mirror, full glass, mixed mirror/glass, and unavailable-ray fallback where `local=1`, `mirror=transmission=0`. Test `IsClosedTriangleMesh` with the generated cube and with one triangle removed.

- [ ] **Step 2: Extend `WorldRendererRoutingTest` to assert scene extraction preserves dense material identities and all clear-glass scalar/color fields without copying texture bytes.**

- [ ] **Step 3: Add GL integration cases before implementation.** The ray-effects self-test must cover full mirror removing local diffuse, glass IOR 1.52 bending a background marker through entry/exit, grazing Fresnel exceeding normal-incidence Fresnel, total internal reflection, reference-distance absorption, and open-mesh reflection fallback.

- [ ] **Step 4: Run the focused standalone and integrated tests; expect failures for absent optical packing and transport.**

- [ ] **Step 5: Build `FRenderScene::materialsByIdentity` during scene extraction and a dense primary material-optics table during ray-scene packing.** Index zero of the vector corresponds to material identity one. Keep hit-material records compatible with instance/triangle selection, expand them from six to eight texels for secondary material optics, and include all new fields in `materialHash_`.

- [ ] **Step 6: Reshape backend upload resources.** GL3.3 exposes `primaryOpticsTexels` through a buffer texture; GL4.3 exposes the identical vec4 array through an SSBO. Validate material identity before indexing and use opaque defaults for zero/out-of-range identity.

- [ ] **Step 7: Implement mirror replacement.** Trace one reflected ray, shade one opaque continuation hit with the shared point-light function or sample the environment, and store the already weighted `kr * reflected` contribution rather than adding `mirrorFactor * reflection` to full raster color.

- [ ] **Step 8: Implement dielectric entry/exit transport in both shaders.** Orient normals against incident rays, use Snell's law and Schlick Fresnel, find the next intersection on the same instance/material as the exit, apply `BeerLambertFromTransmittance` semantics over the measured inside distance, refract back to air, and shade one continuation hit or environment miss. Total internal reflection uses reflection weight one.

- [ ] **Step 9: Implement transparent shadow traversal.** Opaque hits return zero visibility. Translucent hits with `castRayTracedShadows=false` are skipped; enabled glass multiplies shadow throughput by `(1-Fresnel) * beer(insideDistance)` and continues along the original light direction. Support one transmissive closed volume plus one remaining-segment opaque/miss test; a second transmissive volume blocks because multi-volume dielectric transport is outside this milestone.

- [ ] **Step 10: Validate closed transmissive meshes in `FRaySceneCache`.** For each BLAS used by a translucent material, count undirected indexed edges and require exactly two incident triangles per edge. Cache the result by mesh asset ID/revision; add one deterministic warning string to `FPackedRayScene::warnings` for invalid meshes. Extend `IRayTracingBackend.cpp` so `FRayEffectsScheduler` forwards each warning through its existing deduplicating warning sink. The shader uses reflection fallback for those records.

- [ ] **Step 11: Replace temporary additive reflection composition.** Read `localWeight` from optical alpha and produce `emissive + localWeight * (ambient + selectedDirect + GI) + opticalContribution.rgb`. Use local weight one and optical RGB zero when the target is absent.

- [ ] **Step 12: Re-run material, routing, scheduler, GL3.3, and optional GL4.3 tests.** Require both backends to match optical probes within the existing ray-effect tolerance.

- [ ] **Step 13: Commit optical transport.**

```powershell
git add Engine/Render/FRenderScene.* Engine/Render/FRenderMath.* Engine/RayTracing/FRaySceneCache.* Engine/Render/FRenderOutputs.h Engine/Render/IRayTracingBackend.* Engine/Render/UGL33RayTracingBackend.* Engine/Render/UGL43RayTracingBackend.* Engine/Render/Shaders/RayEffectsFragmentShaders.h Engine/Render/Shaders/RayEffectsComputeShaders.h Engine/Render/Shaders/RasterLightingShaders.h Engine/Render/URasterLightingPass.cpp Test/MaterialOpticsTest.cpp Test/WorldRendererRoutingTest.cpp Test/RayEffectsSchedulingTest.cpp Test/main.cpp
git commit -m "feat: add mirror and clear glass ray transport"
```

## Task 6: Add Internal HDR Composition, ACES Presentation, and Shared SSAA

**Files:**

- Create: `Engine/Render/UHybridPresentationPass.h`
- Create: `Engine/Render/UHybridPresentationPass.cpp`
- Create: `Engine/Render/Shaders/HybridPresentationShaders.h`
- Create: `Test/HybridPresentationContractTest.cpp`
- Modify: `Engine/Render/URasterLightingPass.h`
- Modify: `Engine/Render/URasterLightingPass.cpp`
- Modify: `Engine/Render/UWorldRenderer.h`
- Modify: `Engine/Render/UWorldRenderer.cpp`
- Modify: `Engine/Editor/EditorEngine.cpp`
- Modify: `Engine/Framework/GameEngine.cpp`
- Modify: `Test/WorldRendererRoutingTest.cpp`
- Modify: `Test/main.cpp`
- Modify: all six Visual Studio project/filter files for the three new engine files

**Interfaces:**

```cpp
struct FInternalRenderSize
{
    int width = 0;
    int height = 0;
};

FInternalRenderSize ResolveInternalRenderSize(
    int outputWidth, int outputHeight, int ssaa, int maxTextureSize);

class UHybridPresentationPass
{
public:
    bool Init(std::uint64_t contextGeneration, std::string* diagnostic = nullptr);
    bool Resize(int internalWidth, int internalHeight,
                std::uint64_t contextGeneration, std::string* diagnostic = nullptr);
    bool CompositeHDR(const UHardwareGBuffer& gbuffer,
                      const FRasterLightingOutput& raster,
                      const FRayEffectOutputs& rayEffects,
                      const FRenderQuality& quality,
                      FRenderOutputView& hdrOutput,
                      std::string* diagnostic = nullptr);
    bool Present(const FRenderOutputView& hdrInput,
                 FRenderTarget& outputTarget,
                 const FRenderQuality& quality,
                 std::string* diagnostic = nullptr);
    void Shutdown() noexcept;
};
```

- [ ] **Step 1: Write `HybridPresentationContractTest`.** Test size resolution at 1x/2x, overflow/GL-limit rejection, mirror/glass composition weights from Task 5, ACES monotonicity, and exactly one linear-to-sRGB display conversion marker in the presentation shader.

- [ ] **Step 2: Extend `WorldRendererRoutingTest` so target dimensions remain output dimensions while the executor observes `internalWidth = target.Width()*ssaa` and `internalHeight = target.Height()*ssaa`.**

- [ ] **Step 3: Extend `--raster-lighting-selftest` with an HDR light greater than one.** Assert the internal readback exceeds one before presentation and the displayed result remains bounded and non-white after ACES.

- [ ] **Step 4: Run the focused tests; expect failure because composite currently clamps/gammas directly into the caller target.**

- [ ] **Step 5: Move hybrid composition out of `URasterLightingPass` into `UHybridPresentationPass`.** Allocate one `RGBA16F` internal HDR texture/FBO. For covered pixels calculate `emissive + localWeight*(ambient + selectedDirect + GI) + opticalContribution`; for uncovered pixels copy sky. Do not clamp or gamma-convert this target.

- [ ] **Step 6: Implement the presentation shader.** Sample internal HDR with linear filtering at output resolution, multiply by `exp2(exposureEV)`, apply the same ACES coefficients as `FRenderMath`, and convert linear RGB to exact piecewise sRGB:

```glsl
vec3 linearToSRGB(vec3 x)
{
    bvec3 cutoff = lessThanEqual(x, vec3(0.0031308));
    vec3 low = 12.92 * x;
    vec3 high = 1.055 * pow(max(x, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, cutoff);
}
```

- [ ] **Step 7: Make `UWorldRenderer` compute internal dimensions from sanitized quality while preserving the caller target as output size.** Resize G-buffer, raster outputs, ray outputs, and hybrid HDR to internal size; present once to the already bound caller target.

- [ ] **Step 8: Remove Editor's `w * ssaa`/`h * ssaa` target sizing.** Create/resize `viewportTarget_` at visible `w,h` and let `UWorldRenderer` own the internal resolution decision.

- [ ] **Step 9: Load `SSAA` in `GameEngine::OnStartup` through `ReadRenderQuality`; keep `backbufferTarget_` at window size.** SSAA 2 therefore renders internal 2x and resolves to the actual backbuffer.

- [ ] **Step 10: Preserve depth-view behavior.** Route depth visualization directly to presentation without accumulation, denoising, exposure, or ACES.

- [ ] **Step 11: Re-run presentation, routing, raster-lighting, and Editor/Game production-route gates; expect output/internal sizes and HDR probes to pass.**

- [ ] **Step 12: Commit the shared presentation path.**

```powershell
git add Engine/Render/UHybridPresentationPass.* Engine/Render/Shaders/HybridPresentationShaders.h Engine/Render/URasterLightingPass.* Engine/Render/UWorldRenderer.* Engine/Editor/EditorEngine.cpp Engine/Framework/GameEngine.cpp Test/HybridPresentationContractTest.cpp Test/WorldRendererRoutingTest.cpp Test/main.cpp Engine.vcxproj Engine.vcxproj.filters Test/Test.vcxproj Test/Test.vcxproj.filters Template/Template.vcxproj Template/Template.vcxproj.filters
git commit -m "feat: add hdr presentation and shared ssaa"
```

## Task 7: Add Temporal Sequencing, Accumulation, and Edge-Aware Denoising

**Files:**

- Create: `Engine/Render/FRenderHistory.h`
- Create: `Engine/Render/FRenderHistory.cpp`
- Create: `Engine/Render/URayEffectsReconstruction.h`
- Create: `Engine/Render/URayEffectsReconstruction.cpp`
- Create: `Engine/Render/Shaders/RayEffectsReconstructionShaders.h`
- Create: `Test/RenderHistoryTest.cpp`
- Modify: `Engine/Render/IRayTracingBackend.h`
- Modify: `Engine/Render/URasterLightingPass.h`
- Modify: `Engine/Render/URasterLightingPass.cpp`
- Modify: `Engine/Render/UGL33RayTracingBackend.cpp`
- Modify: `Engine/Render/UGL43RayTracingBackend.cpp`
- Modify: `Engine/Render/Shaders/RayEffectsFragmentShaders.h`
- Modify: `Engine/Render/Shaders/RayEffectsComputeShaders.h`
- Modify: `Engine/Render/UWorldRenderer.h`
- Modify: `Engine/Render/UWorldRenderer.cpp`
- Modify: `Test/main.cpp`
- Modify: all six Visual Studio project/filter files for the five new engine files

**Interfaces:**

```cpp
struct FTemporalFrame
{
    std::uint64_t signature = 0;
    std::uint32_t frameIndex = 0;
    bool reset = true;
};

std::uint64_t BuildRenderHistorySignature(
    const FRenderScene& scene,
    const FRenderFeatures& features,
    const FRenderQuality& quality,
    int internalWidth,
    int internalHeight,
    ERayTracingBackend backend,
    std::uint64_t contextGeneration,
    std::uint64_t environmentRevision);

class FTemporalSequence
{
public:
    FTemporalFrame Begin(std::uint64_t signature, std::uint32_t frameCap);
    void Reset() noexcept;
};

class URayEffectsReconstruction
{
public:
    bool Reconstruct(const UHardwareGBuffer& gbuffer,
                     const FRayEffectOutputs& raw,
                     const FTemporalFrame& frame,
                     const FRenderQuality& quality,
                     std::uint64_t contextGeneration,
                     FRayEffectOutputs& reconstructed,
                     std::string* diagnostic = nullptr);
    void Reset() noexcept;
    void Shutdown() noexcept;
};
```

- [ ] **Step 1: Write `RenderHistoryTest.cpp`.** Build a minimal scene and assert an unchanged second frame advances from index 0 to 1; changing camera, transform, geometry revision, material scalar, texture path/stamp, light, environment, feature mask, quality, size, backend, or context changes the signature and returns index 0 with `reset=true`.

- [ ] **Step 2: Extend ray GL tests with static convergence and reset probes.** Read the same soft-shadow/GI pixel for 32 frames and assert variance falls; then move the camera and assert the exposed frame index resets to zero and no old-color ghost remains.

- [ ] **Step 3: Run the history and GL tests; expect failures because random hashes are frame-invariant and no history exists.**

- [ ] **Step 4: Implement deterministic history hashing.** Hash exact float bits, mesh asset IDs/revisions, transforms, resolved material properties, texture path plus last-write timestamp, lights, environment settings/revision, feature and quality fields, dimensions, backend, and context. Never hash entire texture byte arrays per frame. Add `URasterLightingPass::EnvironmentRevision()` and increment it only when the effective HDRI/procedural environment resource changes.

- [ ] **Step 5: Add `historySignature` and `frameIndex` to `FRayEffectInputs`; pass `uFrameIndex` to both ray shaders.** Include frame index in every stochastic shadow/GI seed while preserving backend parity.

- [ ] **Step 6: Implement reconstruction resources.** Allocate ping-pong `RGBA16F` histories for shadowed direct and GI, plus scratch textures/FBOs at size/context changes. A reset clears history before the current raw frame contributes. A stable frame uses running-average weight `1/min(frameIndex+1, temporalFrames)`.

- [ ] **Step 7: Implement two bilateral shadow passes and three A-trous GI passes.** Use G-buffer depth, geometric normal, object identity, and material identity. Reject different identities and samples whose absolute depth difference exceeds `max(0.01, 0.02 * abs(centerDepth))`; reject normal dot products below `0.8` and otherwise multiply by `pow(normalDot, 32)`. Use separable shadow steps 1 and 2, and A-trous GI steps 1, 2, and 4. Keep perfect mirror/glass optical output temporally reusable only for an unchanged signature and never spatially blur it.

- [ ] **Step 8: Integrate reconstruction between raw ray evaluation and HDR composition.** On ray-backend failure reset sequence/reconstruction and pass empty optional effects. On reconstruction failure reset history and forward the valid current raw outputs for that frame.

- [ ] **Step 9: Add reconstruction resource counters to `FWorldRendererStats` and the performance gate.** Assert warm-up/resize allocations occur, then 100 unchanged frames allocate zero new textures/framebuffers.

- [ ] **Step 10: Re-run history, ray-effects, performance, and state-restoration gates; expect all to pass on GL3.3 and capability-gated GL4.3.**

- [ ] **Step 11: Commit temporal reconstruction.**

```powershell
git add Engine/Render/FRenderHistory.* Engine/Render/URayEffectsReconstruction.* Engine/Render/Shaders/RayEffectsReconstructionShaders.h Engine/Render/IRayTracingBackend.h Engine/Render/URasterLightingPass.* Engine/Render/UGL33RayTracingBackend.cpp Engine/Render/UGL43RayTracingBackend.cpp Engine/Render/Shaders/RayEffectsFragmentShaders.h Engine/Render/Shaders/RayEffectsComputeShaders.h Engine/Render/UWorldRenderer.* Test/RenderHistoryTest.cpp Test/main.cpp Engine.vcxproj Engine.vcxproj.filters Test/Test.vcxproj Test/Test.vcxproj.filters Template/Template.vcxproj Template/Template.vcxproj.filters
git commit -m "feat: reconstruct temporal ray effects"
```

## Task 8: Add sRGB Mipmaps and Capability-Bounded Anisotropy

**Files:**

- Create: `Engine/Render/FTextureSamplingPolicy.h`
- Create: `Engine/Render/FTextureSamplingPolicy.cpp`
- Create: `Test/TextureSamplingPolicyTest.cpp`
- Modify: `Engine/Render/UHardwareRasterizer.cpp`
- Modify: `Engine/Render/Shaders/HardwareRasterShaders.h`
- Modify: `Engine/RayTracing/FRaySceneCache.h`
- Modify: `Engine/RayTracing/FRaySceneCache.cpp`
- Modify: `Engine/Render/UGL33RayTracingBackend.cpp`
- Modify: `Engine/Render/UGL43RayTracingBackend.cpp`
- Modify: `Engine/Render/Shaders/RayEffectsFragmentShaders.h`
- Modify: `Engine/Render/Shaders/RayEffectsComputeShaders.h`
- Modify: `Test/main.cpp`
- Modify: all six Visual Studio project/filter files for the two new engine files

**Interfaces:**

```cpp
struct FTextureSamplingPolicy
{
    bool anisotropySupported = false;
    float requestedAnisotropy = 1.0f;
    float maximumAnisotropy = 1.0f;
    float EffectiveAnisotropy() const;
};

std::vector<unsigned char> ResampleRGBA8ToLayer(
    const Material& source, int targetWidth, int targetHeight);
```

- [ ] **Step 1: Write `TextureSamplingPolicyTest`.** Assert unsupported anisotropy returns 1, requested 8/max 16 returns 8, requested 8/max 4 returns 4, malformed capabilities return 1, and full-layer resampling fills every target texel without black unused borders.

- [ ] **Step 2: Add integrated GL assertions.** Query raster and ray material textures after upload and require `GL_TEXTURE_MIN_FILTER=GL_LINEAR_MIPMAP_LINEAR`, a complete mip level chain, sRGB internal format, and anisotropy no greater than the implementation maximum.

- [ ] **Step 3: Run focused and GL tests; expect failure because textures currently use linear base-level sampling and the ray array contains padded sub-rectangles.**

- [ ] **Step 4: Change raster base-color upload to `GL_SRGB8_ALPHA8`, generate mipmaps, and set trilinear minification.** Remove manual `pow(texture, 2.2)` from hardware/raster shaders so hardware sRGB decoding occurs exactly once.

- [ ] **Step 5: Probe `GL_EXT_texture_filter_anisotropic` once per context and apply `min(requested, reportedMaximum)` to raster material textures.** Missing extension is a silent 1x/trilinear fallback.

- [ ] **Step 6: Change `FRaySceneCache` material-array packing to one fully populated normalized-UV layer per source texture.** Resample each source to the common layer width/height, remove logical sub-rectangle metadata, and update `alb()` in both ray shaders to sample `vec3(addressedUV, layer)` directly.

- [ ] **Step 7: Upload the ray material array as `GL_SRGB8_ALPHA8`, generate its complete mip chain, and apply the same anisotropy policy in both backends.** Preserve the 256 MiB packing budget and transactional rollback behavior.

- [ ] **Step 8: Re-run policy, upload-failure, material-atlas, GL3.3, GL4.3, and oblique high-frequency texture probes; expect all to pass.**

- [ ] **Step 9: Commit texture filtering.**

```powershell
git add Engine/Render/FTextureSamplingPolicy.* Engine/Render/UHardwareRasterizer.cpp Engine/Render/Shaders/HardwareRasterShaders.h Engine/RayTracing/FRaySceneCache.* Engine/Render/UGL33RayTracingBackend.cpp Engine/Render/UGL43RayTracingBackend.cpp Engine/Render/Shaders/RayEffectsFragmentShaders.h Engine/Render/Shaders/RayEffectsComputeShaders.h Test/TextureSamplingPolicyTest.cpp Test/main.cpp Engine.vcxproj Engine.vcxproj.filters Test/Test.vcxproj Test/Test.vcxproj.filters Template/Template.vcxproj Template/Template.vcxproj.filters
git commit -m "feat: filter material textures across raster and ray paths"
```

## Task 9: Expose the Corrected Settings in Editor and Game

**Files:**

- Modify: `Engine/Editor/EditorEngine.h`
- Modify: `Engine/Editor/EditorEngine.cpp`
- Modify: `Engine/Framework/GameEngine.cpp`
- Modify: `Engine/Framework/FProjectDescriptor.cpp`
- Modify: `Setting/DefaultEngine.ini`
- Modify: `Setting/DefaultGame.ini`
- Modify: `Template/Setting/DefaultEngine.ini`
- Modify: `Template/Setting/DefaultGame.ini`
- Create: `Test/RenderQualityPersistenceTest.cpp`
- Modify: `Test/RenderSettingsMigrationTest.cpp`

**Editor labels:**

```text
Material:
  Blend Mode: Opaque | Translucent
  Opacity
  Refraction
  Transmittance Color
  Transmittance Distance
  Cast Ray Traced Shadows
  Legacy Mirror

Render Quality:
  Anti-Aliasing: Off (1x) | SSAA 2x
  Shadow Samples
  GI Samples
  Temporal Frames
  Exposure EV
  Anisotropy

Render Features:
  Ray-Traced Translucency
```

- [ ] **Step 1: Write `RenderQualityPersistenceTest.cpp`.** Round-trip every quality field through `FRenderQualitySettings`, verify absent new keys receive corrected defaults, and verify invalid INI values sanitize identically for Editor and Game.

- [ ] **Step 2: Extend project/world migration tests for the visible `Ray-Traced Translucency` setting and template defaults.**

- [ ] **Step 3: Run persistence/migration tests; expect failure while Editor owns private readers and Game omits SSAA/new fields.**

- [ ] **Step 4: Replace `EditorEngine.cpp` local quality readers/writers with `FRenderQualitySettings`.** Add controls for `Exposure EV`, `Temporal Frames`, and `Anisotropy`; retain the 1x/2x SSAA combo.

- [ ] **Step 5: Make `GameEngine::OnStartup` call the same reader for `[Render]`.** Load SSAA, exposure, temporal frame cap, anisotropy, and all existing values in one operation.

- [ ] **Step 6: Add clear-glass fields to the shared `DrawMaterialFields` path and remove the duplicate per-instance material widget block by routing it through `DrawMaterialFields`.** Disable translucent-only controls when Blend Mode is Opaque. Label `Mirror` as `Legacy Mirror` with guidance that PBR migration will use Metallic/Roughness.

- [ ] **Step 7: Add `Ray-Traced Translucency` to Project Settings and world render-feature controls.** When RT is unavailable, show that translucent materials use the safe opaque/local fallback rather than disappearing.

- [ ] **Step 8: Update root/template INI files with exact defaults and run persistence, migration, full build, and one manual Editor material-save/reload check.**

- [ ] **Step 9: Run the project generation/packaging regression.**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Test/ScriptPackagingTest.ps1
```

- [ ] **Step 10: Commit the shared authoring and persistence UI.**

```powershell
git add Engine/Editor/EditorEngine.* Engine/Framework/GameEngine.cpp Engine/Framework/FProjectDescriptor.cpp Setting/DefaultEngine.ini Setting/DefaultGame.ini Template/Setting/DefaultEngine.ini Template/Setting/DefaultGame.ini Test/RenderQualityPersistenceTest.cpp Test/RenderSettingsMigrationTest.cpp
git commit -m "feat: expose hybrid quality and glass settings"
```

## Task 10: Lock Regression, Performance, Documentation, and Packaging

**Files:**

- Modify: `Test/main.cpp`
- Modify: `Test/RenderPerformanceInvariantTest.cpp`
- Modify: `Test/WorldRendererRoutingTest.cpp`
- Modify: `Test/RayEffectsSchedulingTest.cpp`
- Modify: `docs/rendering/hardware-raster-ray-effects.md`
- Modify: `README.md`

- [ ] **Step 1: Add final mutation-resistant assertions.** Require no `shadowVisibilityTarget`/whole-raster visibility multiply, one shared point-light source, one display conversion, no steady-state allocations after warm-up, and raster-only neutral fallback for every optional output.

- [ ] **Step 2: Run the complete standalone matrix and runner self-tests.**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneRunnerSelfTest.ps1
```

- [ ] **Step 3: Build `Engine.sln` as `Debug|Win32` and run all integrated gates.**

```powershell
& .\bin\Test.exe --meshrevisiontest
& .\bin\Test.exe --deprecated-raytracer-lifecycle-selftest
& .\bin\Test.exe "--hw-raster-selftest=$renderEvidence\hardware.ppm"
& .\bin\Test.exe "--raster-lighting-selftest=$renderEvidence\lighting.ppm"
& .\bin\Test.exe "--ray-effects-selftest=$renderEvidence\rays.ppm"
& .\bin\Test.exe --render-performance-selftest
& .\bin\Test.exe --ray-compute-init-selftest
```

- [ ] **Step 4: Verify the visual acceptance scenes.** Capture numeric probes and images for colored/distance lights, isolated ambient/emissive shadows, 32-frame soft-shadow/GI convergence, camera-reset behavior, full mirror, clear glass, colored transmissive shadows, oblique high-frequency texture, and Editor/Game SSAA 1/2.

- [ ] **Step 5: Update the renderer guide.** Document the exact frame graph, formulas, history invalidation inputs, material fields, clear-glass limitations, quality defaults, Editor/Game call stacks, GL3.3/4.3 parity, fallback behavior, and the assignment-facing source map.

- [ ] **Step 6: Update README verification commands and explain that `Legacy Mirror` is transitional while clear glass uses Unreal-familiar translucent inputs.**

- [ ] **Step 7: Generate a project in a temporary path with spaces and build both configurations.**

```powershell
$generatedParent = Join-Path ([IO.Path]::GetTempPath()) ('hybrid-generated-' + [Guid]::NewGuid())
New-Item -ItemType Directory -Path $generatedParent | Out-Null
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/GenerateProject.ps1 -Parent $generatedParent -Name HybridQualityGame
& $msbuild "$generatedParent\HybridQualityGame\HybridQualityGame.sln" /m:1 /nr:false /p:Configuration=Editor /p:Platform=Win32 /clp:ErrorsOnly
& $msbuild "$generatedParent\HybridQualityGame\HybridQualityGame.sln" /m:1 /nr:false /p:Configuration=Game /p:Platform=Win32 /clp:ErrorsOnly
```

- [ ] **Step 8: Run packaging regression and verify the frozen project keeps all new engine sources/shaders while excluding Developer Settings.**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Test/ScriptPackagingTest.ps1
```

- [ ] **Step 9: Inspect the final diff for generated artifacts, accidental `Test/Config/` staging, stale global-shadow symbols, manual texture gamma, and unrelated user changes.**

```powershell
git diff --check
git status --short
rg -n "shadowVisibilityTarget|raster \* visibility|pow\(texture\([^\n]*2\.2" Engine/Render Engine/RayTracing
```

- [ ] **Step 10: Commit final regression locks and documentation.**

```powershell
git add Test/main.cpp Test/RenderPerformanceInvariantTest.cpp Test/WorldRendererRoutingTest.cpp Test/RayEffectsSchedulingTest.cpp docs/rendering/hardware-raster-ray-effects.md README.md
git commit -m "test: lock hybrid renderer quality behavior"
```

## Final Completion Conditions

- The full standalone matrix, runner self-tests, Debug Win32 build, all integrated GL gates, generated Editor/Game builds, and packaging regression exit successfully.
- GL3.3 produces the complete corrected renderer; GL4.3 output probes match it within the documented tolerance.
- Static stochastic scenes converge, every specified change resets history, and no frame-stable renderer resource allocates after warm-up.
- Raster-only mode remains complete and safe, including opaque/local fallback for mirror and translucent materials when ray effects are unavailable.
- The final branch contains only intentional renderer, material, configuration, test, and documentation changes; `Test/Config/` remains untouched.
