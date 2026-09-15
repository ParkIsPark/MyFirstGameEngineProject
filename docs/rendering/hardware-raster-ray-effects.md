# Hardware raster and secondary ray effects

This is the current rendering and submission map. Earlier design plans are historical records. The two normal product modes are **Raster** and **Raster + Ray Effects**. Both Editor and Game use hardware raster primary visibility; the latter adds selected secondary shadows, GI, reflection, and clear-glass transport. CPU software raster and whole-frame pure GPU ray tracing are deprecated, developer-only teaching routes. The normal frame never renders a CPU image to bridge into OpenGL.

## Exact frame graph and color contract

`internalSize = outputSize * SSAA`, where SSAA is 1 or 2. Every scene-space pass uses the internal size; only the last pass resolves to output size.

```text
UHardwareRasterizer::RenderGeometry
  -> UHardwareGBuffer (coverage/position, geometric normal + ambient.r,
     shading normal/model, albedo/shininess, specular/mirror,
     exact object/material identity, precomputed direct + ambient.g,
     emissive + ambient.b, depth)
URasterLightingPass::Render
  -> RGBA16F environmentAmbientTarget + RGBA16F unshadowedDirectTarget
FRayEffectsScheduler::Execute (only when requested)
  -> raw shadowedDirectTarget + GI target + optical target
URayEffectsReconstruction::Reconstruct
  -> temporal shadow/GI history, 2-pass bilateral shadow, 3-pass A-trous GI
UHybridPresentationPass::CompositeHDR
  -> RGBA16F linear HDR
UHybridPresentationPass::Present
  -> linear SSAA resolve -> exposure -> ACES fitted -> linearToSRGB once
```

The point-light implementation is injected from `Shaders/SharedLightingShaderSource.h` into hardware Flat/Gouraud pre-lighting, raster Phong, GL3.3 fragment rays, and GL4.3 compute rays:

```text
d2 = max(dot(lightPosition - position, lightPosition - position), 0.01)
radiance = lightSource / d2
diffuse = albedo * radiance * max(dot(N,L), 0)
specular = specularColor * radiance * pow(max(dot(N,normalize(L+V)),0), max(shininess,1))
direct = diffuse + specular
shadowedDirect = sum(lightVisibility[i] * direct[i])
```

Composition never multiplies the whole raster result by visibility:

```text
kt = translucent ? 1 - clamp(opacity,0,1) : 0
kr = (1-kt) * clamp(LegacyMirror * ReflectionStrength,0,1)
kl = 1-kt-kr
local = environmentAmbient + selectedDirect + GI
HDR = emissive + kl*local + optical.rgb
optical.rgb = kr*reflection + kt*(Fresnel*reflection + (1-Fresnel)*Beer*refraction)
```

`selectedDirect` is reconstructed shadowed direct when valid, otherwise raster unshadowed direct. Missing GI is black. Missing optical contribution uses alpha 1 and RGB black, preserving fully local opaque rendering. An uncovered raster-lighting pixel contains procedural/HDRI sky in ambient, direct RGB `(0,0,0)`, and direct alpha `1`; alpha is intentionally initialized even though direct-light meaning is RGB.

Presentation evaluates `exposed = HDR * exp2(ExposureEV)`, applies the ACES fitted curve, then performs the engine's only linear-to-sRGB conversion. Base-color textures are uploaded as sRGB and decoded once by sampling. HDR environments and numeric buffers remain linear; no material/ray shader applies manual gamma.

## Build, capabilities and routing

Use Windows, VS2022 with C++ workload/Windows SDK, C++17, and Win32. Dependency setup is vendored: headers in `include`, libraries in `lib`, runtime DLLs beside the executable in `bin`, and upstream Lua 5.4.9 in `ThirdParty/Lua`. No dependency download is part of the build; a suitable graphics driver is still required.

`Engine::Init` tries OpenGL 4.3 compatibility and then 3.3 compatibility, initializes GLEW after making the context current, probes the actual version/entry points and establishes a context-generation identity. Below 3.3 is fatal. Hardware geometry/G-buffer, raster lighting/composite and the Compatible ray backend use GLSL 330. Compute uses GLSL 430, SSBOs and image outputs on a capable context.

| Request | Selected behavior | Failure behavior |
|---|---|---|
| RT master off | Raster only; no ray pass | No ray factory/init/allocation/upload/draw/dispatch/barrier work. Already cached ray resources may remain owned until switch/shutdown. |
| RT on / Auto | ComputeGL43 when version, Compute/SSBO support and all required entry points pass; otherwise CompatibleGL33 | Capability fallback records a reason. Compute initialization failure tries Compatible once and latches the successful fallback. |
| RT on / Compatible | CompatibleGL33 fragment pass, even on a capable 4.3 driver | Failed initialization disables ray effects for that request and retains raster output. |
| RT on / Compute | ComputeGL43 | Ineligible or failed initialization disables ray effects; forced Compute does not fall back to Compatible. |

A recoverable backend upload/draw failure clears that frame's secondary outputs, keeps raster output, records a reason and retries next frame. The scheduler deduplicates warnings. A backend being allocated is not proof it contributed to the current frame: inspect effective backend, reason and call/draw/dispatch deltas. `activeRayBackend=Auto` represents no active ray execution for the current raster-only/disabled frame, not an unresolved capability choice.

## Editor and Game call stacks

Context and capability setup precede either route:

```text
Template/main.cpp or Test/main.cpp
  -> Engine::Init
     -> GLFW compatibility context (4.3 preferred, 3.3 fallback)
     -> make current -> glewInit -> ProbeGraphicsCapabilities
     -> context generation -> SelectRayTracingBackend
```

The Editor route uses an offscreen texture destination:

```text
Engine::Run (Editor role) -> EditorEngine::OnStartup
  -> worldRenderer_.Init + local Developer Settings + editor world load/build
frame -> EditorEngine::Render -> DrawUI -> DrawViewport
  -> choose ActiveWorld and Editor/PIE quality -> update camera
  -> viewportTarget_.Resize -> ResolveRayTracingBackend
  -> Developer override controller (None -> normal callback)
  -> UWorldRenderer::Render
     -> normalize hardware primary -> BuildRenderPipelinePlan
     -> ExtractRenderScene (one immutable snapshot)
     -> FRenderTarget::Begin (save caller FBO/viewport)
     -> FHardwareWorldRenderExecutor::Execute
        -> UGPUMeshCache::BeginFrame -> PrepareEnvironment -> UHardwareGBuffer::Resize
        -> UHardwareRasterizer::RenderGeometry
           -> UGPUMeshCache::Acquire -> indexed draws -> ReleaseUnused
        -> URasterLightingPass::Render
        -> optional FRayEffectsScheduler::Execute
           -> factory -> GL33 fragment or GL43 Compute secondary backend
        -> URayEffectsReconstruction::Reconstruct
        -> UHybridPresentationPass::CompositeHDR -> Present -> target
     -> FRenderTarget::End (restore caller FBO/viewport)
  -> ImGui::Image(viewportTarget_.ColorTexture())
```

The Game route loads the saved world and uses the backbuffer:

```text
Template Game main or Editor executable --game <world>
  -> GameEngine::Init -> Engine::Run
     -> GameEngine::OnStartup (renderer + Game quality)
     -> Engine::BootWorld -> GameEngine::WorldSetting / project StartupWorld
        -> FWorldSerializer -> script subsystem -> UWorld::BeginPlay
frame -> world Tick -> GameEngine::Render
  -> camera/frustum + backbufferTarget_.Resize
  -> ResolveRayTracingBackend(world named features)
  -> UWorldRenderer::Render -> same extraction/planner/cache/G-buffer/
     raster lighting/optional scheduler/backend/composite chain above
  -> default framebuffer -> swap
exit -> world EndPlay -> GameEngine::OnShutdown -> subsystem shutdown
```

`Play (Window)` saves the active scene to a temporary world and starts the current Editor executable with `--game`; it therefore works without building a separate Game artifact. Generated outputs now have independent names, `bin/<Project>-Editor.exe` and `bin/<Project>-Game.exe`. DLL lookup remains colocated and the entry point still resolves the project root. The Game entry dispatches only GameEngine and never loads local Developer Settings. The shared project still compiles Editor/developer source units; the optimized Game link removes their unused routes. The resulting Game binary is checked for absence of the Developer Settings route/path.

The explicit Developer override branch is:

```text
Editor DeveloperSettingsPanel -> FDeveloperSettings -> FDeveloperOverrideController
  -> None: invoke shared UWorldRenderer normal callback
  -> SoftwareRasterizer / PureGPURayTracer:
     CreateDeveloperWorldRenderRoute -> FDeprecatedWorldRenderExecutor
     -> legacy implementation; readiness check and isolated teardown
```

Deprecated CPU software raster, whole-frame `UMeshRayTracer`, and CPU-G-buffer `UHybridPass` are educational/regression implementations. The hybrid implementation has no third Developer override. Normal Editor/Game settings cannot select these old modes. Override failure disables the route with a diagnostic; the following valid frame returns to hardware rendering.

## Shading, extraction and reuse

Flat evaluates a face-oriented/precomputed lighting result; Gouraud interpolates vertex lighting; Phong uses interpolated normals for per-pixel lighting. G-buffer semantics retain position/coverage, geometric and shading normals/model, albedo/shininess, specular/mirror, object/material identity, precomputed lighting and emissive, plus depth. Materials resolve component overrides, shared assets and mesh material slots. Textures, wrap behavior and component UV tiling remain part of that path. Multiple point lights contribute subject to measured device limits; truncation warns. Environment/HDRI provides sky and ambient radiance; ray GI/reflections/shadows add the selected secondary contribution.

`UMesh::AssetId` is an immutable cache key and `GeometryRevision` is an explicit invalidation signal. Generators/importers finalize geometry. Call `MarkGeometryDirty` or `FinalizeGeometry` after edits; changing transforms or materials does not imply changing vertices/indices. `Material::RuntimeRevision` is likewise runtime-only and monotonic. Engine loaders, imports, editor commits, and cache replacements call `MarkRuntimeDirty`; code that mutates the currently public `texData`, dimensions, channels, or texture metadata directly must call it after the mutation. Raster and ray steady-frame signatures use this revision plus identity, dimensions, channels, path, and file timestamp, and never scan texture payload bytes. No per-frame geometry hash or readback is used. Extraction borrows large texture and triangle-slot arrays for the duration of one render call while copying scalar/transform state.

The deterministic performance fixture creates exactly 1, 100 and 1000 Actors referencing **one** `GenerateCube` result. Its 24 vertices/36 indices produce one resident GPU mesh resource and one initial upload, then one indexed draw per instance. Repeated/transform-only frames add no vertex/index uploads; one geometry revision adds one reupload. Separate clicks on Add/Cube currently create distinct UMesh assets; those have separate identities. Clones and shared resolved assets can reuse a mesh. Do not describe the shared-mesh fixture as automatic deduplication of independently generated meshes. Multi-material geometry can require multiple contiguous-material draws.

Ray scene caching similarly keeps one mesh-local BLAS per identity/revision and separates instance, material and output-size updates. RT master off has no ray pass; turning it off after use stops work while previously created resources may stay cached. Geometry, material, HDRI, ray outputs and target attachments are released by their owning objects. Context changes invalidate generation-labelled GL names; renderers reject mismatched target generations.

GL3.3 stores exact object/material identity bits in instance texel 3 and reads them with `floatBitsToUint`; it therefore has no separate typed instance-identity TBO. GL4.3 still consumes `FPackedRayScene::instanceIdentityTexels` through its typed `uvec4` SSBO. This is intentional backend packing, not a logical-output difference. Both backends are required to match effect masks and float probes within `0.015 + 0.025 * max(abs(a), abs(b))` per channel.

## Temporal history, glass, quality, and fallback

The reset-on-change history signature covers output/internal dimensions; camera view and projection; geometry, instance transform, material, and texture revisions; point-light positions/source values; environment texture/revision and environment settings; feature mask; shadow/GI/reflection/exposure/SSAA quality; selected backend; and OpenGL context generation. Backend failure/fallback, shutdown, resize, SSAA change, and context recreation also reset history. A reset contributes frame index 0; an unchanged signature advances toward the configured 32-frame running-average cap. There is no motion-vector reprojection, so camera/object changes reset immediately rather than ghosting prior pixels. Identity, depth, and geometric-normal rejection prevent reconstruction from crossing object and strong geometric edges. Mirror/refraction remains deterministic and is not spatially blurred.

Clear glass uses `Blend Mode = Translucent`, `Opacity`, `Refraction`, `Transmittance Color`, `Transmittance Distance`, and `Cast Ray Traced Shadows`. The absorption conversion is `sigmaA=-log(clamp(color,.0001,1))/max(distance,.0001)` and `Beer=exp(-sigmaA*travelledDistance)`. Transport supports one closed-volume entry, same-instance/material exit search, one continuation hit/environment miss, Schlick Fresnel, and Snell refraction. Exit-boundary total internal reflection continues inside that same volume for at most four surface encounters, accumulating Beer distance, rather than substituting the unrelated camera-side entry reflection. An open/malformed volume or exhausted bounded search falls back to reflection and emits the existing deduplicated diagnostic. Transmissive shadows apply Fresnel and Beer attenuation without bending the shadow ray. Masked/general alpha blending, sorting, nested dielectrics, recursive mirrors, rough/frosted optics, metallic PBR, and caustics are out of scope.

`Legacy Mirror` is the transitional compatibility scalar `mirrorFactor`; a value of 1 replaces local light when ray reflections are available. Clear glass does not overload it. When the requested ray backend is unavailable or a pass fails, mirror and glass weights fall back to local 1 / mirror 0 / transmission 0, shadow falls back to raster direct, and GI/optical outputs are neutral. No optional failure may blacken or remove the raster surface.

Defaults and accepted ranges are:

| Setting | Default | Accepted |
|---|---:|---:|
| SSAA | 1 | 1 or 2 |
| ShadowSamples | 4 | 1..16 |
| ShadowSoftness | 0.05 | nonnegative |
| GISamples | 4 | 0..32 |
| GIBounces | 1 | 0..4 |
| GIStrength | 1.0 | artistic multiplier |
| ReflectionStrength | 1.0 | 0..1 |
| ExposureEV | 0.0 | -16..16 |
| TemporalFrames | 32 | 1..32 |
| Anisotropy | 8 | 1..16, capped by device |

## Telemetry and verification

Read `UWorldRenderer::Stats()` (or Editor/Game `RendererStats()`) in a debugger. These getters expose read-only snapshots; they do not change render decisions or perform GL reads. Keep a value copy before the next frame to compare deltas.

| Counter group | Meaning |
|---|---|
| `hardwareDrawCalls`, `geometryUploads`, `geometryReuploads` | Lifetime indexed geometry submissions, initial vertex/index uploads and revision reuploads. One upload represents the vertex/index pair. |
| `hardwareGBufferPasses`, `rasterLightingPasses`, `compositePasses` | Successful completed passes. |
| `rayFactoryCalls`, `rayBackendInitializations`, `rayBackendCalls` | Lifetime creation, initialization attempts and render attempts. |
| `rayDraws`, `rayDispatches`, `rayMemoryBarriers` | Fragment draws versus Compute dispatches and their visibility barriers; Compute does not also increment rayDraws. |
| `rayResourceAllocations`, `rayReleasedResources` | Actual nonzero GL object names created and deleted, including temporary shaders, failed Init, rolled-back candidates, replacements and shutdown. Not GPU bytes or calls returning no name. Context abandonment is not deletion. |
| `rayBufferUploadCalls`, `rayTextureUploadCalls` | Actual submitted `glBufferData` and `glTexImage2D/3D` calls, including calls whose transaction later fails. Texture storage allocation with null pixels is included. |
| `raySceneUploadAttempts`, `rayBLASUploadAttempts`, `rayInstanceUploadAttempts`, `rayMaterialUploadAttempts`, `rayOutputAllocationAttempts` | Started candidate transactions/categories, including failed/retried work. Validation may reject an attempt before any GL upload call; use the call counters for actual submitted GL operations. |
| `raySceneUploads`, `rayBLASUploads`, `rayInstanceUploads`, `rayMaterialUploads`, `rayOutputAllocations` | Successfully committed transactions only. They intentionally do not count rejected candidates and must not be used as actual-work counters. Output allocations count committed resize/reconfiguration transactions, not current textures. |
| `residentGeometryResources`, `liveGBuffer*`, `liveMaterialTextures`, `liveEnvironmentTextures`, `liveRaster*`, `liveRayOutputTextures`, `liveRayBLAS` | Current live ownership; returns to zero at owner shutdown. Cumulative totals do not reset. |
| target `OwnedAttachmentCount()` | Caller-owned target objects: texture viewport owns FBO/color/depth (3); default framebuffer owns none (0). |
| `cpuFramebufferGenerations`, `cpuReadbacks`, `cpuFramebufferUploads` | Zero in every normal hardware frame. |
| `activeRayBackend`, `backendReason` | Effective backend and selected/fallback/disable/failure explanation. |

`--render-performance-selftest` uses real OpenGL and production UWorldRenderer, tests 1/100/1000 shared cubes, raster/Compatible/Auto routes, off-after-on, ineligible backend, three BeginPlay/EndPlay cycles for each backend, repeated 64x64 -> 96x64 -> 64x64 targets, shutdown and restart. It also runs bounded production Editor and Game hooks with RT off/on, isolating local settings in a temporary directory. After warm-up, 100 size-stable frames must add zero ray GL allocations, buffer/texture uploads, or reconstruction allocations. Stable live counts with all effects and a textured/HDRI scene are geometry 1, G-buffer textures 9 + FBO 1, material texture 1, HDRI 1, raster outputs 2 + FBO 1, hybrid HDR 1 + FBO 1, ray output textures 3, reconstruction textures 8 + FBO 1, BLAS 1, target attachments 3. These are structural ownership invariants, not a GPU byte-memory or timing benchmark.

From repository root after `Engine.sln Debug|Win32`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneRunnerSelfTest.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Test/ScriptPackagingTest.ps1
.\bin\Test.exe --meshrevisiontest
.\bin\Test.exe --fbxtest
.\bin\Test.exe --deprecated-raytracer-lifecycle-selftest
.\bin\Test.exe --hw-raster-selftest=hardware.ppm
.\bin\Test.exe --raster-lighting-selftest=lighting.ppm
.\bin\Test.exe --ray-effects-selftest=rays.ppm
.\bin\Test.exe --render-performance-selftest
.\bin\Test.exe --ray-compute-init-selftest
```

Standalone tests own their own main and are outside Test.vcxproj. The runner documents compiler/linker commands and bounds each run to 60 seconds. Its outer finally removes only its dedicated artifact directory on compile failure or exception; per-process finally terminates only its owned process when necessary, waits, drains stdout/stderr and disposes handles. Logs and original compiler/process status survive. All gcc, g++ and cmd/MSVC calls share stderr-safe invocation with immediate exit capture for Windows PowerShell 5.1. Run `powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneRunnerSelfTest.ps1` for actual C and C++ compiler failures in all three branches plus safe start/nonzero/timeout/post-start exception probes. No arbitrary command-injection parameter is provided. Integrated gates exit after their checks; never run the no-argument interactive editor as a regression gate. Ray/hardware/lighting switches require the documented `=output.ppm` argument. The PPM gates intentionally read test output; production frames do not. Delete generated verification images after inspection. Serial generated builds should alternate `/t:Build /p:Configuration=Editor`, Game, Editor, Game with `/p:Platform=Win32 /m:1 /nr:false` and `_CL_=/FS`; both EXEs must stay present with unchanged hashes on the final up-to-date builds.

The ray gate injects real initialization, output-allocation and scene-upload failures in both implementations. Actual-work totals rise before rollback; committed revision/upload counters and prior live outputs remain unchanged. Retry records a second attempt and commits once. With a valid owning context, created/deleted name totals balance after shutdown, including retired scheduler backends. `liveRayBLAS` describes successfully committed GPU scene residency, not a CPU-packed candidate waiting for upload.

For startup failures check the actual GL version/capability diagnostic, GPU driver and DLLs beside the executable. The Editor status bar and viewport show the effective RT backend (or Disabled/raster only) separately from the requested selection. The viewport persistently displays `backendReason`; the status bar also exposes it as a tooltip. Unsupported explicit selection, initialization failure/fallback, and effect-pass failure remain visible even after one-time logging is suppressed. A retained initialized backend with a failed effect pass is not advertised as RT On; a successful retry restores its effective label. UI reflects the last completed frame. Unknown project `RayTracingBackend` values warn and fall back to Auto, matching world loading. Explicit Compatible is a useful 3.3 diagnostic even on a 4.3 machine. For black output verify positive target size, current generation, camera orientation and G-buffer gate; then run raster and ray gates separately. A missing HDRI/material texture reports its own upload diagnostic. Long Windows build paths can exceed MSVC limits; generate verification projects under a short temporary parent.

Upload paths isolate the caller's unpack PBO and all baseline alignment/row/image/skip layout fields, restoring them on success and failure. Lighting uploads use a captured texture unit, never an uncaptured caller unit 10+. Render-target allocation and Begin/End preserve distinct read/draw framebuffer bindings. Compatible effect FBOs select `GL_NONE` for reading before completeness checks, including GI-only and reflection-only masks. The ray gate enforces the GL3.3 read-buffer rule on actual FBOs even when the driver implements a newer relaxed rule, and exercises hostile-state allocation, refresh, resize, rollback and exact uploaded texels. This is not a claim of testing on a physical 3.3-only driver.

## File-by-file engine and assignment map

Paths are relative to the repository root; paired headers define the contracts implemented by each source.

| Files | Responsibility |
|---|---|
| `Template/main.cpp`, `Test/main.cpp` | Entry role dispatch; bounded integration gates live in Test. |
| `Engine/Framework/Engine.cpp/.h` | Context, GLEW, capability selection, world/subsystem lifecycle. |
| `Engine/Render/FGraphicsCapabilities.cpp/.h`, `FGL43ComputeApi.cpp/.h` | Actual driver capabilities, backend policy and Compute entry-point validation. |
| `Engine/Editor/EditorEngine.cpp/.h` | AddActor, camera, clone/Play/Stop, viewport and shared renderer call. |
| `Engine/Framework/GameEngine.cpp/.h` | Saved-world runtime, quality, shared renderer/backbuffer call. |
| `Engine/Render/UWorldRenderer.cpp/.h` | One scene extraction, normal hardware executor, optional scheduler, aggregate telemetry. |
| `Engine/Render/FRenderFeatures.h`, `FRenderQuality.h`, `FRenderPipelinePlan.cpp/.h` | Named feature/quality contract and ordered pass plan. |
| `Engine/Render/FRenderScene.cpp/.h`, `FRenderOutputs.h` | Immutable per-call scene and backend-neutral output semantics. |
| `Engine/Render/UGPUMeshCache.cpp/.h`, `FGPUMeshResource.h` | Identity/revision cache, upload adapter, ownership counters. |
| `Engine/Render/UHardwareRasterizer.cpp/.h`, `UHardwareGBuffer.cpp/.h` | Real VAO/VBO/EBO uploads, indexed instance/material draws, GPU attachments. |
| `Engine/Render/URasterLightingPass.cpp/.h`, `Shaders/HardwareRasterShaders.h`, `Shaders/RasterLightingShaders.h`, `Shaders/SharedLightingShaderSource.h` | Split ambient/direct raster lighting and the one shared point-light source. |
| `Engine/Render/URayEffectsReconstruction.cpp/.h`, `FRenderHistory.cpp/.h` | History signatures, temporal accumulation, bilateral/A-trous reconstruction, invalidation. |
| `Engine/Render/UHybridPresentationPass.cpp/.h`, `Shaders/HybridPresentationShaders.h`, `FRenderMath.cpp/.h` | Optical/local energy weights, linear HDR composition, SSAA resolve, exposure, ACES, sole display conversion. |
| `Engine/Render/IRayTracingBackend.cpp/.h` | Scheduler, fallback/failure latching, backend contract and cumulative totals. |
| `Engine/Render/UGL33RayTracingBackend.cpp/.h`, `Shaders/RayEffectsFragmentShaders.h` | Secondary fragment ray effects with buffer textures. |
| `Engine/Render/UGL43RayTracingBackend.cpp/.h`, `Shaders/RayEffectsComputeShaders.h` | Secondary compute effects, SSBO/images and memory barrier. |
| `Engine/RayTracing/FRaySceneCache.cpp/.h`, `Engine/Acceleration/BVH.cpp/.h` | Mesh BLAS, instances/TLAS/material revisions and acceleration layout. |
| `Engine/Render/FRenderTarget.cpp/.h` | Move-only target ownership, resize, binding restore and context validity. |
| `Engine/Mesh/UMesh.cpp/.h`, `UMeshComponent.cpp/.h`, `Material.cpp/.h`, `UMaterial.cpp/.h` | Cube generators/revisions, shared references, material/texture assets. |
| `Engine/World/UWorld.cpp/.h`, `AActor.cpp/.h`, `USceneComponent.cpp/.h` | Spawn/list/transform ownership, C++ Actor rotation/lifecycle and script dispatch. |
| `Engine/Serialization/FWorldSerializer.cpp/.h`, `Engine/Framework/FProjectDescriptor.cpp/.h` | Format 1/2 migration to named format 3; script/project defaults preservation. |
| `Engine/Developer/FDeveloperSettings.cpp/.h`, `FDeveloperWorldRenderRoute.cpp/.h`, `Engine/Editor/DeveloperSettingsPanel.cpp/.h`, `Engine/Core/FFeatureLifecycle.cpp/.h` | Local preferences, explicit overrides and lifecycle badges/warnings. |
| `Engine/Render/FDeprecatedWorldRenderExecutor.cpp/.h`, `URenderer.cpp/.h`, `UHybridPass.cpp/.h`, `Engine/RayTracing/UMeshRayTracer.cpp/.h`, `Engine/Rasterizer/URasterizer.cpp/.h` | Explicitly deprecated educational implementation and regression boundaries. |
| `Engine/Script/*`, `Engine/Editor/FEditorScriptWorkflow.cpp` | Project Lua integration; see the linked scripting guide for individual files. |
| `Scripts/GenerateProject.ps1`, `Template/Template.vcxproj`, `Template/Package.ps1` | Independent Editor/Game artifacts, linked/package paths, Lua packaging. |
| `Test/RenderPerformanceInvariantTest.cpp`, `RunStandaloneTests.ps1`, `RunStandaloneRunnerSelfTest.ps1`, `main.cpp`, `Fixtures/box.fbx` | Structural tests, standalone/failure-cleanup recipes, real-GL gates and deterministic import fixture. |

Assignment item 1, cube creation/draw: `EditorEngine::AddActor("Cube", name)` -> `UMesh::GenerateCube` -> `UMeshComponent`/`AActor` -> `UWorld::Spawn` -> `ExtractRenderScene` -> `UWorldRenderer::Render` -> `UHardwareRasterizer::RenderGeometry` -> `UGPUMeshCache::Acquire` -> `glDrawElements`. Item 2, multiple cubes: Actor list -> one immutable extraction -> shared cache for shared asset identities -> one indexed draw per cube instance. Item 3, current click action: viewport selection reaches `EditorEngine::PickActor`; clicking Add/Cube uses `camera.eye + normalize(-camera.w) * 8`, then follows the draw path above. **Arbitrary viewport-surface click-position placement is not implemented by this migration.** Left-click ray selection and camera-forward Add are not a completed literal click-position rubric.

Rotation is authored in C++ `AActor::Tick`, reached through `UWorld::Tick`. The Actor-only Lua milestone exposes `Engine.Log` only. Its lifecycle pointer is `Engine::Run/BootWorld` (or Editor CopyWorld/OnPlay) -> `UWorld::BeginPlay/Tick/EndPlay` -> `AActor::Dispatch*` -> `UScriptComponent` -> isolated `FLuaScriptInstance` callbacks. World format 3 preserves script attachment configuration; VM/closures are runtime-only. See [Lua Actor scripting](../scripting/lua-actor-scripting.md).

This map describes project engine/integration responsibilities, not blanket authorship. Upstream `ThirdParty`, `include`, library/DLL binaries, Dear ImGui/ImGuizmo, GLEW/GLFW/GLM/Assimp, stb and Lua 5.4.9 retain their authorship/licenses. The project integrates these dependencies and supplies the described engine behavior; do not claim vendored code or hardware/driver work as original coursework. Lua provenance is in [ThirdParty/Lua/README](../../ThirdParty/Lua/README.md).
