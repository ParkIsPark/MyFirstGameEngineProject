# Hardware Raster and Ray-Traced Effects Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the normal CPU framebuffer renderer with OpenGL hardware rasterization, preserve the visible raster feature set, and make ray tracing an optional group of effects layered on top of raster output.

**Architecture:** `UWorldRenderer` is the only world-render entry point for editor and standalone builds. It builds a pass plan from named render features, sends scene geometry through a revision-aware GPU cache, produces a GPU G-buffer with OpenGL rasterization, then optionally executes either an OpenGL 3.3-compatible ray-effects backend or an OpenGL 4.3 compute backend. The CPU software rasterizer and pure GPU ray tracer remain available only as deprecated developer overrides.

**Tech Stack:** C++17, OpenGL 3.3 core minimum, optional OpenGL 4.3 Compute/SSBO, GLFW/GLAD, GLSL, Dear ImGui, existing standalone C++ tests and Visual Studio projects.

**Spec:** `docs/superpowers/specs/2026-09-13-hardware-raster-lua-scripting-design.md`

**Execution corrigendum:** `docs/superpowers/plans/2026-09-14-hardware-raster-ray-effects-corrigendum.md` is authoritative where this pre-Lua plan names stale formats, files, interfaces, toolchain details, or task order.

## Global Constraints

- Preserve Flat, Gouraud, and Phong shading; materials, textures, UV tiling, multiple lights, and environment/HDRI rendering.
- With ray tracing disabled, schedule no ray-tracing pass and issue no ray-tracing buffer upload.
- With ray tracing enabled, rasterization remains responsible for primary visibility and the G-buffer; ray tracing supplies shadows, GI, and reflections.
- Upload unchanged meshes only once. Transform-only edits must not re-upload vertex/index data.
- Do not expose the CPU software rasterizer or pure GPU ray tracer in normal user-facing renderer choices.
- Persist normal render features in world/project data; persist deprecated overrides only in a local developer-settings file.
- Add every new engine `.h` and `.cpp` to `Engine.vcxproj`, `Test/Test.vcxproj`, `Template/Template.vcxproj`, and their `.filters` files in the same task that creates it. Standalone `Test/*_test.cpp` files keep their own `main()` and are not added to `Test.vcxproj`.
- Keep all existing user changes and `docs/index.bleve/` untouched.

## Verification Command Convention

Every new standalone test file must start with its complete MSYS2 UCRT64 compile command, including every production `.cpp` it links. Run that command from the repository root with `C:\msys64\ucrt64\bin\g++.exe`, then run the emitted `.exe`; a red step expects compilation or assertions to fail and a green step expects exit code 0. For integrated OpenGL work, build and run with:

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild Engine.sln /m /p:Configuration=Debug /p:Platform=Win32 /clp:ErrorsOnly
& .\bin\Test.exe <self-test arguments>
```

---

## Task 1: Replace Integer Render Modes with Named Features and a Pure Pass Planner

**Files:**

- Create: `Engine/Render/FRenderFeatures.h`
- Create: `Engine/Render/FRenderPipelinePlan.h`
- Create: `Engine/Render/FRenderPipelinePlan.cpp`
- Create: `Test/RenderPipelinePlanTest.cpp`
- Modify: all six Visual Studio project and filter files listed in Global Constraints

**Interfaces:**

```cpp
enum class ERayTracingBackend { Auto, CompatibleGL33, ComputeGL43 };
enum class ELegacyRendererOverride { None, SoftwareRasterizer, PureGPURayTracer };

struct FRenderFeatures {
    bool rayTracing = false;
    bool rayTracedShadows = true;
    bool rayTracedGI = true;
    bool rayTracedReflections = true;
    ERayTracingBackend rayTracingBackend = ERayTracingBackend::Auto;
};

enum class ERenderPass {
    HardwareGBuffer,
    RasterLighting,
    RayTracedEffects,
    Composite
};

std::vector<ERenderPass> BuildRenderPipelinePlan(const FRenderFeatures& features);
```

- [ ] Write `RenderPipelinePlanTest.cpp` asserting raster-only gives `HardwareGBuffer, RasterLighting, Composite`, and enabled RT inserts exactly one `RayTracedEffects` pass before `Composite`.
- [ ] Compile and run the standalone test; expect failure because the new headers do not exist.
- [ ] Implement the enums, settings object, and side-effect-free planner. Treat a master RT switch with all three child effects disabled as raster-only.
- [ ] Re-run the test; expect exit code 0.
- [ ] Add the files to every project/filter file and build `Debug|Win32` with MSBuild.
- [ ] Commit: `git add Engine/Render/FRenderFeatures.h Engine/Render/FRenderPipelinePlan.* Test/RenderPipelinePlanTest.cpp Engine.vcxproj Engine.vcxproj.filters Test/Test.vcxproj Test/Test.vcxproj.filters Template/Template.vcxproj Template/Template.vcxproj.filters && git commit -m "refactor: define feature-based render pipeline"`

## Task 2: Introduce a Uniform Feature-Lifecycle Registry

**Files:**

- Create: `Engine/Core/FFeatureLifecycle.h`
- Create: `Engine/Core/FFeatureLifecycle.cpp`
- Create: `Test/FeatureLifecycleTest.cpp`
- Modify: `Engine/Core/EngineVersion.h`
- Modify: project/filter files

**Interfaces:**

```cpp
enum class EFeatureLifecycle { Stable, Experimental, Deprecated, Internal };

struct FFeatureDescriptor {
    std::string id;
    std::string displayName;
    EFeatureLifecycle lifecycle;
    std::string replacement;
    std::string warning;
};

class FFeatureLifecycleRegistry {
public:
    static const FFeatureDescriptor& Require(std::string_view id);
    static const std::vector<FFeatureDescriptor>& All();
};

#define ENGINE_DEPRECATED(message) [[deprecated(message)]]
```

- [ ] Write a test requiring descriptors for `renderer.cpu_software` and `renderer.gpu_pure_raytracer`; assert both are Deprecated and have non-empty replacement/warning text.
- [ ] Compile/run; expect missing declarations.
- [ ] Implement a static immutable registry, duplicate-ID validation, and the portable compile-time macro. Register hardware raster as Stable and the two legacy paths as Deprecated.
- [ ] Set the engine's feature-policy version to `2.0` without changing unrelated product version strings.
- [ ] Run the test and full build; expect both to pass.
- [ ] Commit: `git add Engine/Core/FFeatureLifecycle.* Engine/Core/EngineVersion.h Test/FeatureLifecycleTest.cpp **/*.vcxproj* && git commit -m "feat: add feature lifecycle metadata"`

## Task 3: Migrate World and Project Render Settings

**Files:**

- Modify: `Engine/Core/UScene.h`
- Modify: `Engine/Serialization/FWorldSerializer.cpp`
- Modify: `Engine/Framework/FProjectDescriptor.*`
- Modify: project/template configuration files containing `RenderMode`
- Create: `Test/RenderSettingsMigrationTest.cpp`
- Create: `Test/Fixtures/WorldFormat1RenderModes.json`

**Serialization contract:**

```json
{
  "WorldFormat": 2,
  "RenderFeatures": {
    "RayTracing": false,
    "RayTracedShadows": true,
    "RayTracedGI": true,
    "RayTracedReflections": true,
    "Backend": "Auto"
  }
}
```

Legacy mapping is fixed: `RenderMode=0` becomes RT off; `1` becomes RT on with all effects; `2` becomes RT on with all effects. Saving always writes format 2 and never writes `RenderMode`.

- [ ] Add a fixture with all three format-1 modes and a test for the exact mapping plus a format-2 save/load round trip.
- [ ] Run the test; expect failure because `UScene` still exposes an integer mode.
- [ ] Replace normal runtime use of `renderMode` with `FRenderFeatures`, implement tolerant legacy reads, named backend string parsing, and format-2 writes.
- [ ] Update project/template defaults to hardware raster with RT disabled.
- [ ] Run migration tests and open/save one existing sample world; assert the second save is byte-stable apart from established formatting.
- [ ] Commit: `git add Engine/Core/UScene.h Engine/Serialization/FWorldSerializer.cpp Engine/Framework/FProjectDescriptor.* Test/RenderSettingsMigrationTest.cpp Test/Fixtures && git commit -m "refactor: migrate worlds to named render features"`

## Task 4: Detect Graphics Capabilities and Select a Backend

**Files:**

- Create: `Engine/Render/FGraphicsCapabilities.h`
- Create: `Engine/Render/FGraphicsCapabilities.cpp`
- Create: `Test/GraphicsBackendSelectionTest.cpp`
- Modify: engine OpenGL initialization source
- Modify: project/filter files

**Interfaces:**

```cpp
struct FGraphicsCapabilities {
    int major = 0;
    int minor = 0;
    bool computeShaders = false;
    bool shaderStorageBuffers = false;
};

struct FBackendSelection {
    ERayTracingBackend requested;
    ERayTracingBackend selected;
    std::string fallbackReason;
};

FBackendSelection SelectRayTracingBackend(
    ERayTracingBackend requested,
    const FGraphicsCapabilities& capabilities);
```

- [ ] Test Auto on 4.3 selects Compute, Auto on 3.3 selects Compatible, forced Compute on 3.3 falls back with a reason, and anything below 3.3 is rejected.
- [ ] Run the pure selection test; expect failure.
- [ ] Implement capability probing after GL context creation and keep selection logic free of GL calls for testing.
- [ ] Make engine startup emit a clear fatal diagnostic for OpenGL below 3.3 and one non-fatal warning for a forced-compute fallback.
- [ ] Run tests and start once with `--rt-backend=gl33`; verify the selected backend is logged.
- [ ] Commit: `git add Engine/Render/FGraphicsCapabilities.* Test/GraphicsBackendSelectionTest.cpp **/*.vcxproj* && git commit -m "feat: select ray backend from OpenGL capabilities"`

## Task 5: Add Revision-Aware GPU Mesh Resources

**Files:**

- Create: `Engine/Render/FGPUMeshResource.h`
- Create: `Engine/Render/UGPUMeshCache.h`
- Create: `Engine/Render/UGPUMeshCache.cpp`
- Modify: `Engine/Mesh/UMesh.h`
- Modify: `Engine/Mesh/UMesh.cpp`
- Create: `Test/GPUMeshCacheTest.cpp`
- Modify: project/filter files

**Interfaces:**

```cpp
using FMeshAssetId = std::uint64_t;

struct FGPUMeshResource {
    unsigned vao = 0;
    unsigned vertexBuffer = 0;
    unsigned indexBuffer = 0;
    std::uint64_t uploadedRevision = 0;
    std::size_t indexCount = 0;
};

class UGPUMeshCache {
public:
    const FGPUMeshResource& Acquire(const UMesh& mesh);
    void ReleaseUnused();
    const FGPUMeshCacheStats& Stats() const;
};
```

- [ ] Introduce a fake GL upload adapter and test first acquire uploads, second unchanged acquire does not, transform-only changes do not, and mesh-data revision changes do.
- [ ] Run the test; expect missing cache types.
- [ ] Give mesh data a stable asset ID and monotonic geometry revision. Implement VAO/VBO/EBO ownership and deterministic cleanup through the adapter.
- [ ] Expose upload counts for developer diagnostics without putting GL handles in serialized data.
- [ ] Run the test under repeated acquire/release and the full build.
- [ ] Commit: `git add Engine/Render/FGPUMeshResource.h Engine/Render/UGPUMeshCache.* Test/GPUMeshCacheTest.cpp **/*.vcxproj* && git commit -m "feat: cache mesh geometry on the GPU"`

## Task 6: Produce the G-Buffer with Hardware Rasterization

**Files:**

- Create: `Engine/Render/UHardwareGBuffer.h`
- Create: `Engine/Render/UHardwareGBuffer.cpp`
- Create: `Engine/Render/UHardwareRasterizer.h`
- Create: `Engine/Render/UHardwareRasterizer.cpp`
- Create: `Engine/Render/Shaders/HardwareRasterShaders.h`
- Modify: `Engine/Render/UWorldRenderer.*`
- Modify: project/filter files

**Interfaces:**

```cpp
class UHardwareGBuffer {
public:
    void Resize(int width, int height);
    void BindForGeometry();
    void BindTextures(unsigned firstTextureUnit) const;
    bool IsComplete() const;
};

class UHardwareRasterizer {
public:
    void RenderGeometry(const FRenderScene& scene,
                        UGPUMeshCache& meshCache,
                        UHardwareGBuffer& gbuffer);
};
```

- [ ] Add a hidden `--hw-raster-selftest=<output.ppm>` path that creates a 64x64 context, draws one colored cube through `UHardwareRasterizer`, reads back only for the test, and fails if coverage/depth/normal invariants are absent.
- [ ] Build/run the self-test; expect failure before the classes exist.
- [ ] Implement framebuffer ownership with color/albedo, normal, material, and depth attachments valid on OpenGL 3.3; add resize and completeness checks.
- [ ] Implement indexed cube drawing from `UGPUMeshCache`, camera/object transforms, face winding, depth test, and material/UV attributes.
- [ ] Run the self-test and inspect the generated PPM; expect non-background center pixels and valid depth ordering.
- [ ] Commit: `git add Engine/Render/UHardware* Engine/Render/Shaders/HardwareRasterShaders.h Engine/Render/UWorldRenderer.* **/*.vcxproj* && git commit -m "feat: rasterize world geometry into a GPU gbuffer"`

## Task 7: Restore Raster Lighting and Shading Parity

**Files:**

- Create: `Engine/Render/URasterLightingPass.h`
- Create: `Engine/Render/URasterLightingPass.cpp`
- Create: `Engine/Render/Shaders/RasterLightingShaders.h`
- Modify: `Engine/Render/UWorldRenderer.*`
- Modify: material, light, texture, and environment upload helpers
- Create: `Test/Fixtures/RenderParityScene.json`
- Modify: project/filter files

- [ ] Extend the render self-test to render deterministic Flat, Gouraud, and Phong frames plus texture/UV-tiling, multiple-light, and HDRI variants. Record numeric probes such as center pixel, edge discontinuity, tiled UV repetition, and light contribution deltas.
- [ ] Run it; expect at least the unimplemented modes to fail.
- [ ] Implement raster lighting shaders and state uploads. Keep shading-model selection explicit and ensure texture-less materials take a valid fallback path.
- [ ] Add bounded light upload with a clear warning if a scene exceeds the shader limit, and bind environment textures only when present.
- [ ] Run every parity variant and compare probes to the existing renderer's approved reference captures with documented tolerance.
- [ ] Commit: `git add Engine/Render/URasterLightingPass.* Engine/Render/Shaders/RasterLightingShaders.h Test/Fixtures/RenderParityScene.json Engine/Render/UWorldRenderer.* && git commit -m "feat: preserve raster shading and lighting features"`

## Task 8: Convert Ray Tracing into Optional Effects with a GL 3.3 Backend

**Files:**

- Create: `Engine/Render/IRayTracingBackend.h`
- Create: `Engine/Render/UGL33RayTracingBackend.h`
- Create: `Engine/Render/UGL33RayTracingBackend.cpp`
- Modify: `Engine/Render/UHybridPass.*`
- Modify: `Engine/Render/UMeshRayTracer.*`
- Modify: `Engine/Render/UWorldRenderer.*`
- Create: `Test/RayEffectsSchedulingTest.cpp`
- Modify: project/filter files

**Interfaces:**

```cpp
struct FRayEffectInputs {
    const UHardwareGBuffer& gbuffer;
    const FRenderScene& scene;
    FRenderFeatures features;
};

class IRayTracingBackend {
public:
    virtual ~IRayTracingBackend() = default;
    virtual void RenderEffects(const FRayEffectInputs& inputs,
                               FRayEffectOutputs& outputs) = 0;
};
```

- [ ] Test that raster-only makes zero backend calls and RT-enabled makes one call with the exact child-effect mask.
- [ ] Run it; expect failure because `UWorldRenderer` still couples mode integers to legacy paths.
- [ ] Implement GL 3.3 fragment/TBO scene-data upload and full-screen effect evaluation consuming GPU G-buffer textures directly.
- [ ] Adapt reusable code from `UHybridPass`; annotate legacy whole-frame entry points with `ENGINE_DEPRECATED`. Do not route the normal renderer through a CPU `UGBuffer` or viewport texture upload.
- [ ] Run raster-only and all three individual effect masks; confirm disabled effects do not allocate or dispatch work.
- [ ] Commit: `git add Engine/Render/IRayTracingBackend.h Engine/Render/UGL33RayTracingBackend.* Engine/Render/UHybridPass.* Engine/Render/UMeshRayTracer.* Engine/Render/UWorldRenderer.* Test/RayEffectsSchedulingTest.cpp && git commit -m "feat: layer GL33 ray effects over raster output"`

## Task 9: Add the Optional OpenGL 4.3 Compute Backend

**Files:**

- Create: `Engine/Render/UGL43RayTracingBackend.h`
- Create: `Engine/Render/UGL43RayTracingBackend.cpp`
- Create: `Engine/Render/Shaders/RayEffectsComputeShaders.h`
- Modify: `Engine/Render/UWorldRenderer.*`
- Create: `Test/RayBackendFactoryTest.cpp`
- Modify: project/filter files

- [ ] Test backend factory selection using synthetic capabilities and confirm both implementations satisfy the same interface.
- [ ] Run the test; expect missing compute backend.
- [ ] Implement SSBO packing, compute dispatch, memory barriers, output textures, and resource cleanup. Keep packing definitions shared with CPU-side structs and static-assert sizes/alignments.
- [ ] Add a capability-gated `--ray-effects-selftest`; on GL 4.3+ compare GL33/GL43 output probes within a documented tolerance, otherwise report Skip without failing.
- [ ] Run factory tests and the self-test on available hardware.
- [ ] Commit: `git add Engine/Render/UGL43RayTracingBackend.* Engine/Render/Shaders/RayEffectsComputeShaders.h Engine/Render/UWorldRenderer.* Test/RayBackendFactoryTest.cpp && git commit -m "feat: add compute ray effects backend"`

## Task 10: Make `UWorldRenderer` the Shared Editor and Game Renderer

**Files:**

- Create: `Engine/Render/FRenderTarget.h`
- Modify: `Engine/Render/UWorldRenderer.h`
- Modify: `Engine/Render/UWorldRenderer.cpp`
- Modify: `Engine/Editor/EditorEngine.h`
- Modify: `Engine/Editor/EditorEngine.cpp`
- Modify: game/standalone render loop source
- Create: `Test/WorldRendererRoutingTest.cpp`
- Modify: project/filter files

- [ ] Add an injectable render-device spy and assert editor and standalone routes both call the same `UWorldRenderer::Render(world, camera, target, features)` entry point.
- [ ] Run it; expect the editor's duplicate `RenderWorldGPU` path to violate the assertion.
- [ ] Add an `FRenderTarget` abstraction for window/backbuffer and editor viewport targets. Move scene extraction and pass execution into `UWorldRenderer`.
- [ ] Delete the normal editor CPU framebuffer texture bridge and duplicate GPU scene traversal. Retain deprecated renderer overrides behind an explicit developer-only branch.
- [ ] Run editor and standalone smoke tests; resize both targets and verify no stale attachments or double render.
- [ ] Commit: `git add Engine/Render/FRenderTarget.h Engine/Render/UWorldRenderer.* Engine/Editor/EditorEngine.* Test/WorldRendererRoutingTest.cpp && git commit -m "refactor: share world renderer across editor and game"`

## Task 11: Add Local Developer Settings and Warning Badges

**Files:**

- Create: `Engine/Developer/FDeveloperSettings.h`
- Create: `Engine/Developer/FDeveloperSettings.cpp`
- Create: `Engine/Editor/DeveloperSettingsPanel.h`
- Create: `Engine/Editor/DeveloperSettingsPanel.cpp`
- Modify: editor menu/status UI sources
- Modify: `.gitignore`
- Create: `Test/DeveloperSettingsTest.cpp`
- Modify: project/filter files

**Local contract:**

```ini
[Rendering]
LegacyOverride=None
ShowExperimentalWarnings=true
```

- [ ] Test missing/corrupt local settings fall back to `None`, valid legacy values round-trip, and no serializer output contains `LegacyOverride`.
- [ ] Run it; expect failure.
- [ ] Implement local load/save under a gitignored project-local user settings path. Validate enum strings and log rejected values.
- [ ] Build the Developer Settings panel from the lifecycle registry. Selecting a deprecated override must show an always-visible warning badge naming the supported replacement.
- [ ] Manually verify normal renderer settings do not list legacy paths, and saved worlds/projects remain free of developer overrides.
- [ ] Commit: `git add Engine/Developer Engine/Editor/DeveloperSettingsPanel.* .gitignore Test/DeveloperSettingsTest.cpp **/*.vcxproj* && git commit -m "feat: expose deprecated renderers in developer settings"`

## Task 12: Lock Performance, Regression Coverage, and Documentation

**Files:**

- Create: `Test/RenderPerformanceInvariantTest.cpp`
- Modify: `README.md`
- Modify: `CLAUDE.md`
- Modify: relevant setup/build documentation

- [ ] Add deterministic 1-, 100-, and 1000-cube scenes. Assert one geometry upload per shared mesh revision, zero ray dispatches when RT is off, no CPU framebuffer bridge in normal mode, and stable GPU-resource counts after repeated play/resize cycles.
- [ ] Run the test before final cleanup and record any failing invariant.
- [ ] Fix only the measured violations; expose developer counters for draw calls, geometry uploads, ray dispatches, and fallback reason.
- [ ] Document OpenGL 3.3 minimum, optional 4.3 backend, feature semantics, legacy migration, Developer Settings, and troubleshooting for capability fallback.
- [ ] Run all standalone tests, then build all three projects in `Debug|Win32`. Launch editor and standalone in raster-only and RT-enabled modes.
- [ ] Use `rg -n "renderMode|RenderWorldGPU|vpTex_|CPU framebuffer" Engine` to confirm remaining legacy references are confined to migration or deprecated code.
- [ ] Commit: `git add Test/RenderPerformanceInvariantTest.cpp README.md CLAUDE.md Engine Test && git commit -m "test: lock renderer migration invariants"`

## Final Acceptance Checklist

- [ ] OpenGL 3.3 hardware raster is the default and works in editor and standalone.
- [ ] One cube, multiple cubes, and click-created cubes render through the shared hardware path.
- [ ] Flat/Gouraud/Phong, materials/textures/UV tiling, lights, and environment/HDRI pass parity checks.
- [ ] RT off means raster-only and zero RT work; RT on means raster plus selected ray-traced effects.
- [ ] Auto chooses Compute on capable OpenGL 4.3 hardware and Compatible on OpenGL 3.3.
- [ ] CPU software raster and pure GPU ray tracer are deprecated developer overrides with warning badges.
- [ ] Format-1 worlds load with the specified mapping and re-save as format 2.
- [ ] Unchanged geometry stays GPU-resident and the normal path contains no per-frame CPU framebuffer upload.
- [ ] Full Visual Studio `Debug|Win32` build and all standalone tests pass.
