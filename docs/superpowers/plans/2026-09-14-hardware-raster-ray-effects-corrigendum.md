# Hardware Raster and Ray-Effects Execution Corrigendum

This corrigendum adapts the approved renderer design and 2026-09-13 plan to the repository at `ec5e14e`, after Lua Actor Scripting was merged. Product behavior is unchanged; only stale implementation assumptions and dependency order are corrected.

## Binding decisions

- Keep the existing GLEW + GLFW stack. Do not introduce GLAD. Context creation first attempts OpenGL 4.3 compatibility, then recreates a 3.3 compatibility context if unavailable. Compatibility is retained solely for deprecated renderers during migration.
- OpenGL below 3.3 is fatal. `Auto` prefers Compute when all required 4.3 entry points exist and falls back to Compatible with one warning on capability or initialization failure. Explicit unsupported/failed Compute disables RT for the session with a reason; it never silently changes backend.
- Normal primary visibility is always hardware rasterization. `HardwareRaster = 1` is serialized and normalized to true; it is not a user-disable switch.
- Use the existing sectioned `FArchive` `.world` format. Renderer migration advances saves to `WorldFormat = 3`. Formats 1 and 2 may contain legacy `RenderMode`; both migrate 0 to RT off and 1/2 to RT on with all child effects. Preserve every existing Actor and Component block, especially ScriptComponents. No JSON renderer fixture or second serializer is introduced.
- Serialized backend strings are `Auto`, `Compatible`, and `Compute`. Unknown values warn and use `Auto`.
- `UScene::renderFeatures` is the authoritative saved per-world configuration. `FProjectDescriptor::defaultRenderFeatures` initializes new worlds or worlds without feature fields. `FRenderQuality` is an explicit per-call editor/game profile. `FDeveloperSettings::legacyOverride` is local/session-only and never serialized into world/project data. Remove the editor's duplicate `renderMode_` state.
- Every project/filter edit is additive and must preserve all merged Lua sources, include paths, filters, and `Content\Scripts\**\*.lua` entries. Run `Test/ScriptPackagingTest.ps1` after each project topology change.

## Corrected execution order

Execute original Tasks 1-5 first, then split/move the shared renderer foundation ahead of GPU passes:

1. Feature-based pass planner.
2. Feature lifecycle registry.
3. Sectioned world/project migration to format 3.
4. Capability probing, 4.3-to-3.3 context negotiation, and backend selection with an explicit unavailable/RT-disabled result.
5. Revision-aware GPU mesh cache.
6. **Shared renderer foundation** (the contract/routing portion of original Task 10): `FRenderScene`, scene extractor, `FRenderTarget`, logical G-buffer/raster/ray/composite output contracts, and a single `UWorldRenderer::Render` entry accepting explicit features and quality. Editor and Game must route through it before hardware pass implementation. Legacy execution may temporarily sit behind the orchestrator.
7. Hardware G-buffer (original Task 6).
8. Raster lighting parity (original Task 7).
9. GL 3.3 ray-traced effects (original Task 8).
10. Optional GL 4.3 Compute backend (original Task 9).
11. Finish removal of duplicate normal rendering/CPU upload bridges and deprecate legacy entry points (remainder of original Task 10).
12. Developer Settings and warning badges (original Task 11).
13. Performance/regression/documentation lock (original Task 12).

Each dispatched task receives a task brief reflecting this order; original task numbers remain useful for file lists but do not override these prerequisites.

## Corrected contracts

### Feature lifecycle

Create `Engine/Core/EngineVersion.h`. Use one consistent `FFeatureLifecycleRegistry` name. Descriptors include ID, display name, lifecycle, introduced/deprecated versions, replacement guidance, removal policy, and purpose. Register all five design entries: hardware rasterizer (Stable), ray-traced effects (Stable), OpenGL 4.3 Compute RT (Experimental), CPU software rasterizer (Deprecated), and pure GPU ray tracer (Deprecated).

### Backend selection

`FBackendSelection` must represent unavailable selection, for example with `bool available` and `bool rayTracingEnabled`, in addition to requested/selected and reason. Pure selection is GL-free; startup separately verifies actual required entry points. `--rt-backend=gl33` is implemented only if a real command parser consumes it; otherwise tests exercise the pure selector and startup logs through an existing test seam.

### Mesh revisions

`FMeshGPUCacheStats` and an injected GL upload adapter are part of Task 5. Cache lifetime includes deterministic RAII cleanup and context-generation invalidation. Because `UMesh::vertices` and `indices` are public, geometry revision is advanced through explicit `MarkGeometryDirty`/`FinalizeGeometry` paths and all engine mutators/importers must call it; tests document that unsupported direct external mutation requires an explicit mark. Transform changes never alter mesh revision.

### Shared renderer and target

Task 6 foundation creates the missing types before consumers:

- `FRenderScene`: immutable per-frame mesh instances, transforms, resolved materials/UV tiling, lights, environment, and camera data extracted once from `UWorld`.
- `FRenderTarget`: default framebuffer or texture-backed viewport identity, dimensions, and context generation; resize is explicit.
- raster-lighting output, `FRayEffectOutputs`, and composite input/output contracts with no backend-specific GL resource knowledge in the common orchestrator.
- `UWorldRenderer::Render(world, camera, target, features, quality)` (or one equivalent settings aggregate) is the only normal Editor/Game entry point.

### Logical G-buffer

Freeze the logical fields before implementation: valid/coverage, sampleable depth, world position, geometric/shading normal, resolved albedo, specular color, shininess, mirror factor, shading model, and object/material identity. Physical OpenGL 3.3 attachments may pack these fields but must stay within queried limits and expose typed accessors. Flat uses a per-primitive geometric normal; Gouraud stores/interpolates its vertex-lit contribution; Phong performs per-pixel lighting from interpolated attributes. Ray effects consume world position/depth/material identity and never reconstruct primary visibility independently.

Composite consumes raster lighting plus optional shadow visibility, GI radiance, and reflection radiance. Disabled ray effects allocate/dispatch nothing and behave as neutral inputs.

### Baselines and fixtures

Before replacing the legacy output, capture deterministic numeric probes from the existing renderer. Use a sectioned `.world` fixture plus small deterministic texture and HDRI assets. Record tolerances for center/edge coverage, depth ordering, Flat/Gouraud/Phong differences, UV repetition, additive multi-light contribution, and environment contribution. Integrated GL self-tests are dispatched through `Test/main.cpp` and must terminate with a bounded exit.

## Verification corrections

- UCRT64 standalone commands remain mandatory for pure isolated units. Serializer/editor/routing tests that require the full engine use freshly built Win32 `Engine.lib`; OpenGL tests use integrated `bin/Test.exe` self-test switches.
- Build `Engine.sln` as `Debug|Win32`. Separately build a generated/template project as `Editor|Win32` and `Game|Win32`; Template has no `Debug` configuration.
- Task 8 paths use `Engine/RayTracing/UMeshRayTracer.*`, not `Engine/Render`.
- Task 11 local settings include both `ShowDeprecatedFeatures` and `ShowExperimentalWarnings`. A legacy override actually routes through the deprecated implementation and emits one warning per session.
- Final `rg` verification uses an allowlist for migration/deprecated files rather than requiring zero textual hits.
- Final click-created-cube acceptance means any cube produced by the repository's existing creation workflow renders through the shared hardware path. This renderer plan does not add new click-to-place behavior.
