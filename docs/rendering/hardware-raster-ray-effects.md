# Hardware raster and secondary ray effects

This is the current rendering and submission map. Earlier design plans are historical records. The normal Editor and Game renderer uses hardware raster primary visibility, raster lighting, optional secondary ray effects, and composite. It never renders a CPU image to bridge the normal frame into OpenGL.

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
        -> URasterLightingPass::Composite -> target
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

`UMesh::AssetId` is an immutable cache key and `GeometryRevision` is an explicit invalidation signal. Generators/importers finalize geometry. Call `MarkGeometryDirty` or `FinalizeGeometry` after edits; changing transforms or materials does not imply changing vertices/indices. No per-frame geometry hash or readback is used. Extraction borrows large texture and triangle-slot arrays for the duration of one render call while copying scalar/transform state.

The deterministic performance fixture creates exactly 1, 100 and 1000 Actors referencing **one** `GenerateCube` result. Its 24 vertices/36 indices produce one resident GPU mesh resource and one initial upload, then one indexed draw per instance. Repeated/transform-only frames add no vertex/index uploads; one geometry revision adds one reupload. Separate clicks on Add/Cube currently create distinct UMesh assets; those have separate identities. Clones and shared resolved assets can reuse a mesh. Do not describe the shared-mesh fixture as automatic deduplication of independently generated meshes. Multi-material geometry can require multiple contiguous-material draws.

Ray scene caching similarly keeps one mesh-local BLAS per identity/revision and separates instance, material and output-size updates. RT master off has no ray pass; turning it off after use stops work while previously created resources may stay cached. Geometry, material, HDRI, ray outputs and target attachments are released by their owning objects. Context changes invalidate generation-labelled GL names; renderers reject mismatched target generations.

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

`--render-performance-selftest` uses real OpenGL and production UWorldRenderer, tests 1/100/1000 shared cubes, raster/Compatible/Auto routes, off-after-on, ineligible backend, three BeginPlay/EndPlay cycles for each backend, repeated 64x64 -> 96x64 -> 64x64 targets, shutdown and restart. It also runs bounded production Editor and Game hooks with RT off/on, isolating local settings in a temporary directory. Stable live counts with all three effects and a textured/HDRI scene are geometry 1, G-buffer textures 9 + FBO 1, material texture 1, HDRI 1, raster output 1 + FBO 1, ray output textures 3, BLAS 1, target attachments 3. These are structural ownership invariants, not a GPU byte-memory or timing benchmark.

From repository root after `Engine.sln Debug|Win32`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Test/ScriptPackagingTest.ps1
.\bin\Test.exe --meshrevisiontest
.\bin\Test.exe --fbxtest
.\bin\Test.exe --deprecated-raytracer-lifecycle-selftest
.\bin\Test.exe --hw-raster-selftest=hardware.ppm
.\bin\Test.exe --raster-lighting-selftest=lighting.ppm
.\bin\Test.exe --ray-effects-selftest=rays.ppm
.\bin\Test.exe --render-performance-selftest
```

Standalone tests own their own main and are outside Test.vcxproj. The runner documents compiler/linker commands and bounds each run to 60 seconds. Its outer finally removes only its dedicated artifact directory on compile failure or exception; per-process finally terminates only its owned process when necessary, waits, drains stdout/stderr and disposes handles. Logs and original per-process status survive. Run `powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneRunnerSelfTest.ps1` for real compile failure plus safe start/nonzero/timeout/post-start exception probes. No arbitrary command-injection parameter is provided. Integrated gates exit after their checks; never run the no-argument interactive editor as a regression gate. Ray/hardware/lighting switches require the documented `=output.ppm` argument. The PPM gates intentionally read test output; production frames do not. Delete generated verification images after inspection. Serial generated builds should alternate `/t:Build /p:Configuration=Editor`, Game, Editor, Game with `/p:Platform=Win32 /m:1 /nr:false` and `_CL_=/FS`; both EXEs must stay present with unchanged hashes on the final up-to-date builds.

The ray gate injects real initialization, output-allocation and scene-upload failures in both implementations. Actual-work totals rise before rollback; committed revision/upload counters and prior live outputs remain unchanged. Retry records a second attempt and commits once. With a valid owning context, created/deleted name totals balance after shutdown, including retired scheduler backends. `liveRayBLAS` describes successfully committed GPU scene residency, not a CPU-packed candidate waiting for upload.

For startup failures check the actual GL version/capability diagnostic, GPU driver and DLLs beside the executable. For ray disable/fallback inspect `backendReason` and startup warning. Explicit Compatible is a useful 3.3 diagnostic even on a 4.3 machine. For black output verify positive target size, current generation, camera orientation and G-buffer gate; then run raster and ray gates separately. A missing HDRI/material texture reports its own upload diagnostic. Long Windows build paths can exceed MSVC limits; generate verification projects under a short temporary parent.

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
| `Engine/Render/URasterLightingPass.cpp/.h`, `Shaders/HardwareRasterShaders.h`, `Shaders/RasterLightingShaders.h` | Hardware shading, environment, lighting and final composite. |
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

Assignment item 1, cube creation/draw: `EditorEngine::AddActor("Cube", name)` -> `UMesh::GenerateCube` -> UMeshComponent/Actor -> `UWorld::Spawn` -> shared extraction/cache/hardware route. Item 2, multiple cubes: Actor list -> one immutable extraction -> shared cache for shared asset identities -> one indexed draw per cube instance. Item 3, current click action: clicking Add/Cube uses `camera.eye + normalize(-camera.w) * 8`; the resulting cube enters the same path. **Arbitrary viewport-surface click-position placement is not implemented by this migration.** Left-click ray selection and camera-forward Add are not a completed literal click-position rubric.

Rotation is authored in C++ `AActor::Tick`, reached through `UWorld::Tick`. The Actor-only Lua milestone exposes `Engine.Log` only. Its lifecycle pointer is `Engine::Run/BootWorld` (or Editor CopyWorld/OnPlay) -> `UWorld::BeginPlay/Tick/EndPlay` -> `AActor::Dispatch*` -> `UScriptComponent` -> isolated `FLuaScriptInstance` callbacks. World format 3 preserves script attachment configuration; VM/closures are runtime-only. See [Lua Actor scripting](../scripting/lua-actor-scripting.md).

This map describes project engine/integration responsibilities, not blanket authorship. Upstream `ThirdParty`, `include`, library/DLL binaries, Dear ImGui/ImGuizmo, GLEW/GLFW/GLM/Assimp, stb and Lua 5.4.9 retain their authorship/licenses. The project integrates these dependencies and supplies the described engine behavior; do not claim vendored code or hardware/driver work as original coursework. Lua provenance is in [ThirdParty/Lua/README](../../ThirdParty/Lua/README.md).
