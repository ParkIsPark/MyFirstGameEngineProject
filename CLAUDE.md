# Repository development guide

This repository is a Windows/VS2022 C++17 Win32 OpenGL engine/editor for coursework. The current renderer is hardware raster primary visibility plus optional secondary ray effects. [README](README.md) describes usage; [the rendering guide](docs/rendering/hardware-raster-ray-effects.md) contains the code map, submission boundaries, pass contract and diagnostics.

## Build and project topology

Install the Visual Studio C++ workload and Windows SDK. Headers, `.lib` files, DLLs and Lua source are vendored. Keep Win32: the libraries and solution are 32-bit. `Engine.sln` builds `bin/Engine.lib` and `bin/Test.exe`; the runnable Test project compiles engine sources directly, while standalone tests also consume Engine.lib.

```powershell
$env:_CL_='/FS'
& 'C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe' Engine.sln /t:Build /p:Configuration=Debug /p:Platform=Win32 /m:1 /nr:false
powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Test/ScriptPackagingTest.ps1
```

Use serial MSBuild and `/FS` for reproducible verification. Existing C4819 code-page warnings occur in older comments; standalone recipes use `/utf-8`. Shaders compile at runtime, so a successful C++ build is insufficient: run the bounded real-GL gates listed in README.

`Scripts/GenerateProject.ps1` creates linked projects with Editor/Game Win32 configurations. `Template/Template.vcxproj` sets `TargetName=$(ProjectName)-$(Configuration)`, producing independent `bin/<Project>-Editor.exe` and `bin/<Project>-Game.exe`. Never restore their shared output name: Editor -> Game -> Editor -> Game incremental Build must preserve both roles. DLLs remain colocated in bin. `Play (Window)` launches the current Editor exe with `--game <world>`; no Game build is needed to use that runtime route.

`EngineRoot.props` points to this repository in linked projects and to `$(SolutionDir)` in packaged ones. `OpenglViewer.props` supplies C++17, include/library paths, GLFW WGL macros and `$(SolutionDir)bin` output. `Template/Package.ps1` copies engine/dependencies, safe script content and runtime DLLs; local Developer Settings is not packaged. Open the solution, not an isolated project file.

New engine headers/sources must be registered in **all six** project/filter files: root Engine, Test and Template `.vcxproj`/`.filters`. Preserve existing Lua C sources (compiled as C), Lua include paths, filters and Content entries. A standalone test with its own `main()` belongs outside Test.vcxproj. Run every Test source owning `main()` via its current header recipe and rerun ScriptPackagingTest after topology changes. Do not commit executables, objects, test worlds/images, generated projects or imported scratch models.

## Normal rendering contract

`Engine::Init` negotiates a 4.3 compatibility context then 3.3 compatibility, loads GLEW and probes actual capability. Below 3.3 is fatal. `FGraphicsCapabilities` checks Compute/SSBO support and required entry points. Hardware G-buffer and raster/composite shaders use GLSL 330; the optional compute backend uses GLSL 430 and SSBO/images.

Editor `DrawViewport` and Game `Render` each call **one `UWorldRenderer` implementation**. It extracts one immutable `FRenderScene`, plans named features, obtains geometry from `UGPUMeshCache`, runs `UHardwareRasterizer` into `UHardwareGBuffer`, then `URasterLightingPass`, optional `FRayEffectsScheduler`/backend, and composite into `FRenderTarget`. There is no separate normal Editor renderer. Hardware primary visibility remains mandatory with RT on or off. RT off must never initialize, allocate, upload, draw, dispatch or synchronize any ray backend.

Auto selects Compute when capable and falls back to Compatible for capability or Compute initialization failure. Explicit Compatible remains GL33. Explicit Compute failure disables ray effects for that request/session and retains raster; it never silently changes to Compatible. A recoverable render/upload failure preserves raster and retries the backend next frame. `UWorldRenderer::Stats().activeRayBackend` and `backendReason` report effective routing, including disable/fallback reasons.

Draws, uploads, allocations and scheduler calls are lifetime cumulative, including backend switches and Shutdown/Init. `resident*` and `live*` are current ownership. Targets are caller-owned and report `OwnedAttachmentCount()` separately. No normal frame may increment CPU bridge counters. Counters must observe actual work at the call site, not infer it from Actor counts. Tests deliberately read images for verification; production must not add those reads for telemetry.

`UMesh::AssetId()` is immutable; `GeometryRevision()` changes explicitly. Generators/importers finalize geometry; edits call `MarkGeometryDirty`/`FinalizeGeometry`. Sharing one mesh across 1/100/1000 cube Actors means one geometry upload and one indexed draw per instance. Transform changes alter instance data, not geometry. Do not add per-frame vertex/index hashing. Multi-material meshes can require multiple contiguous-material draws and are outside the one-draw-per-cube fixture assumption. Ray BLAS reuse follows geometry identity/revision; instance/material/target changes invalidate only affected data.

Flat, Gouraud and Phong preserve distinct shading stages. Materials resolve component override/shared asset/per-triangle slots, diffuse texture, UV tiling, emissive/specular/mirror values. Multiple lights respect device limits with diagnostics, and environment/HDRI is GPU resident. Use `textureLod` inside divergent ray loops; implicit derivatives there previously caused driver failures.

## Developer and educational paths

Developer Settings is Editor-only. Its gitignored, local `Config/DeveloperSettings.ini` contains `[Rendering]`: `LegacyOverride=None`, `ShowDeprecatedFeatures=true`, `ShowExperimentalWarnings=true`. Invalid/missing files produce safe defaults and diagnostics. Stable/Experimental/Deprecated/Internal descriptors provide badges and warning policy. Two explicit diagnostic overrides are `SoftwareRasterizer` and `PureGPURayTracer`; None uses the shared hardware route. Do not serialize developer preferences into world/project/game configuration or link the Developer Settings route/path into a Game artifact.

Historical/educational implementations remain for regression: CPU `URasterizer`/`URenderer`, whole-frame `UMeshRayTracer`, and CPU-G-buffer `UHybridPass`. Their old numeric mode vocabulary belongs only in deprecated implementations/tests and migration readers. They are not the normal graded/runtime renderer selection. `FDeprecatedWorldRenderExecutor` and the Developer override controller isolate lifecycle, warn explicitly, and release partial initialization failures.

## World, geometry and scripting

`UWorld` owns `UScene`, camera, physics and Actor lifecycle. Actors own ordered components; typed mesh/light/physics aliases point into that ownership. Root/component transforms use dirty cached parent * relative matrices; setters propagate dirtiness. Rotations are degrees in this bundled GLM. Keep `UMesh::GenerateSphere` ordering and course `FTransform` signed-negative near/far convention intact; do not casually replace the FCG projection with a different convention. CPU mesh intersection remains available for selection/picking.

`FArchive`/factories preserve serialized type names such as PointLight, EnvLight and ScriptComponent. `FWorldSerializer` saves format 3, named `[RenderFeatures]`, camera, Actor/component hierarchy and scripts. Formats 1 and 2 migrate legacy numeric render settings; all new saves use format 3. Unknown/missing keys use defaults. Undo/PIE clones share meshes and preserve hierarchy, ordered components and script configuration.

`UObjImporter` parses OBJ/MTL; `UFbxImporter` integrates upstream Assimp. Missing/corrupt models return an empty result. File imports copy supported assets/sidecars into Content. `UMesh::Resolve` shares procedural descriptors/content assets. Materials support shared `.material` resources; imported MTL saves become `.material` files.

`UScriptSubsystem` owns upstream Lua 5.4.9 and shuts down after world EndPlay. `FLuaBindingRegistry` currently exposes **Engine.Log only**. Do not incidentally expand bindings to Actor/transform/World/Input/render/spawn/physics. Safe libraries are base/coroutine/table/string/math/utf8; io/os/package/debug/dofile/loadfile and per-instance load remain unavailable. Script paths must stay physically under Content/Scripts, including reparse/symlink checks. Compiled bytecode is shared; attachment environments and callbacks are isolated. BeginPlay clears the session cache; no hot reload.

Failures remain component-local. Begin failure disables its instance; Tick failure suppresses later ticks but preserves a successful Begin so End runs exactly once. End failure cannot interrupt cleanup. Diagnostics retain Actor, ScriptComponent index, normalized Path, Phase and Lua message/location. Serialized Enabled/path/order must survive failures and renderer migration; runtime VM/closures never serialize. See [Lua scripting](docs/scripting/lua-actor-scripting.md).

## Submission and troubleshooting boundaries

Add / Cube calls `EditorEngine::AddActor`, builds a `UMesh::GenerateCube` component, places its Actor eight units along camera forward and calls `UWorld::Spawn`. The rendered Actor uses the shared path above. **Arbitrary viewport-surface click-position placement is not implemented by this migration.** Left-click selection is not that rubric item. Cube rotation currently comes from C++ `AActor::Tick`; Lua cannot manipulate transforms.

For black frames or failed ray effects inspect startup GL capability diagnostics and renderer `backendReason`, verify DLLs beside the exe, confirm the active context generation and target size, then run hardware/raster/ray/performance gates. On a 3.3-only driver Auto should select Compatible; forced Compute should retain raster with an explicit disable reason.

Describe engine/integration responsibilities precisely. Do not claim vendored ThirdParty/include/libs/DLLs, Dear ImGui/ImGuizmo, GLEW/GLFW/GLM/Assimp, stb or upstream Lua 5.4.9 as original work, or claim the entire graphics pipeline is authored here. Preserve licenses and provenance. Historical design plans are records, not a substitute for current implementation and verification.
