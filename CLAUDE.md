# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Purpose

This is a self-contained OpenGL engine template for Konkuk University Computer Graphics homework. After cloning, no additional library installation is required — all headers, `.lib` files, and `.dll` files are included.

## Workflows

There are **two** ways to work with this repo, depending on what you're doing:

### A) Make a project (`GenerateProject.bat`)
Double-click `GenerateProject.bat` (Unreal-style — opens a cmd window, then a folder picker, then a name input box). It generates `<picked-folder>\<name>\` containing `<name>.sln` + `.vcxproj` + `.vcxproj.filters` + `main.cpp` + `EngineRoot.props` + `Package.bat` + `Package.ps1` + a `bin\` with the runtime DLLs.

**By default the project is "linked":** `EngineRoot.props` holds an absolute path back to this engine repo, and the build pulls `Engine\`, `include\`, `lib\`, and `OpenglViewer.props` directly from the repo. Engine edits propagate immediately — no re-generation needed.

**To freeze the engine into the project** (e.g. for homework submission), double-click `Package.bat` inside the generated folder. It copies the engine sources in and rewrites `EngineRoot.props` so `$(EngineRootDir)` resolves to `$(SolutionDir)`. The project becomes self-contained; moving or deleting the engine repo will not break it. Re-running `Package.bat` on an already-packaged project is a safe no-op.

Workflow summary:

| Mode | EngineRootDir | Engine files | Engine edits propagate? |
|------|---------------|--------------|-------------------------|
| Linked (default after generate) | absolute path to repo | live in engine repo | Yes |
| Packaged (after `Package.bat`) | `$(SolutionDir)` | copied into project | No |

### B) Explore / verify the engine itself (`Engine.sln`)
Open `Engine.sln` at the repo root. It contains **two** projects: `Engine` (StaticLibrary — compiles all of `Engine\**\*.cpp` to `bin\Engine.lib` purely so compile errors surface immediately; nothing links the `.lib`) and `Test` (Console Application — the runnable demo/editor that compiles the engine `.cpp` files directly via `$(EngineRootDir)` and links them into `bin\Test.exe`). Use this solution to navigate engine code with full IntelliSense, verify it still compiles, and actually run it.

Engine improvements in `Engine\` can be committed back to this repo. **Commit engine source only** — do not commit `bin/` build output, generated `Test\Debug\` artifacts, or imported model files dropped into `bin\`.

### Build & run
```powershell
# Open Engine.sln in VS and press F5 (Test is the startup project), or from a developer prompt:
msbuild Engine.sln      /p:Configuration=Debug /p:Platform=Win32   # builds Engine.lib + Test.exe
msbuild <generated>.sln /p:Configuration=Debug /p:Platform=Win32   # an external generated project
```
Output goes to the solution's `bin\` (co-located with the runtime DLLs). `Test.exe` entry points (`Test\main.cpp`):
- **(no args)** → the ImGui `EditorEngine` (default).
- **`--game <world.path>`** → the headless-of-ImGui `GameEngine`: loads a `.world` and runs it (BeginPlay + physics/actor tick) in the world's saved render mode. This is the standalone game runtime; the editor's **Play (Window)** spawns `argv0 --game` as a separate process via `FProcess`.
- **`--demo`** → mesh demo window, keys `1`=CPU raster silhouette, `2`=GPU mesh ray trace, `3`=depth view, `4`=hybrid (raster G-buffer + GPU shadow), `5`=imported OBJ cube, `6`=imported FBX.
- **`--fbxtest`** → headless FBX-import self-test (no GL window), prints PASS/FAIL gates, exits non-zero on failure.

Building from an agent: `msbuild` is usually not on PATH; resolve it with `vswhere` (e.g. `…\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe`). C4819 codepage warnings are expected (Korean comments) and are not errors — filter with `/clp:ErrorsOnly`. After a build, confirm `bin\Test.exe`'s timestamp updated (a "0 errors" Engine compile does not by itself relink the exe). GLSL is compiled at runtime, so shader changes can only be verified by running `! ./bin/Test.exe`.

### Standalone CPU unit tests (`Test\*_test.cpp`)
Each `Test\<area>_test.cpp` (rasterizer, bvh, physics, framework, threading, culling, hybrid, obj, scenegraph, serialize, mesh_material, shading, light, world_serialize, ini) is an **independent** GL-free self-test with its own `main()` and `[T#] … PASS|FAIL` gate lines. They are **not** part of `Test.vcxproj` (which only builds `main.cpp`); compile/run one at a time with g++ (msys2 ucrt64) — the exact command is in each file's header comment, e.g.:
```bash
g++ -std=c++17 -I include -I Engine/Mesh -I Engine/Rasterizer -I Engine/World -I Engine/RayTracing \
    Test/rasterizer_test.cpp Engine/Mesh/UMesh.cpp Engine/Rasterizer/FTransform.cpp \
    Engine/Rasterizer/URasterizer.cpp -o rasterizer_test && ./rasterizer_test
```

## Repository Structure

```
EngineDevelop/
├── Engine/                  # Tracked engine source — edit and commit freely
│   ├── Framework/           # Engine (window+loop+lifecycle), GameEngine (--game runtime), FProjectDescriptor (.proj/.ini boot), BuildManager (background msbuild), USubsystem(Manager)
│   ├── Core/                # UScene (actors/lights/outputImage/renderMode/shadingModel/skyHDRI), UPostProcessFilter
│   ├── World/               # UWorld (scene+camera+physics+lifecycle), AActor (rootComponent), USceneComponent (scene-graph node), ACamera
│   ├── Mesh/                # UMesh (geometry+generators+BVH+intersect+.mesh I/O+Resolve), UMeshComponent, Vertex, Material
│   ├── Rasterizer/          # FTransform (FCG matrices), URasterizer, UFrameBuffer, UGBuffer
│   ├── Render/              # URenderer (CPU shaded raster orchestrator), UWorldRenderer (renders a UWorld in any mode), UHybridPass, USkyHDRI, FRenderShowFlag, FRenderQuality
│   ├── RayTracing/          # UMeshRayTracer (GPU mesh RT, two-level BVH), RTShading.h, URay.h
│   ├── Acceleration/        # BVH (median-split, stack traversal, GLSL-portable)
│   ├── Serialization/       # FArchive (3-mode save/load), TFactory + REGISTER_ACTOR/COMPONENT, FWorldSerializer (.world), FIniFile (.ini)
│   ├── Threading/           # ThreadPool (header-only worker pool, ParallelForChunks)
│   ├── Import/              # UObjImporter (hand-written), UFbxImporter (Assimp)
│   ├── Editor/              # EditorEngine (Dear ImGui editor) + External/imgui + External/ImGuizmo (vendored), FFileDialog (Win32 picker), FProcess (spawn game)
│   ├── Light/               # ALight, PointLightComponent, EnvironmentLightComponent, LightComponent
│   ├── Physics/             # UPrimitiveComponent, UShape/USphere/UBoxComponent, UPhysicsWorld
│   └── Player/              # UPlayerCharacter (pawn), UPlayerController (input + camera follow)
├── Test/                    # Test.vcxproj (Console app: main.cpp runs editor/demo) + standalone *_test.cpp
├── Template/                # Scaffold (vcxproj + filters + main.cpp + Package.bat/ps1) — used by GenerateProject
├── Scripts/                 # GenerateProject.ps1 (project generator backend)
├── bin/                     # Runtime DLLs incl. assimp-vc143-mt.dll (tracked) + build output (gitignored)
├── include/                 # Third-party headers: GL/GLEW, GLFW, GLM, Assimp, stb_image.h
├── lib/                     # Import libs: glew32, freeglut, glfw3, opengl32, glu32, assimp-vc143-mt
├── OpenglViewer.props       # Shared MSBuild property sheet (C++17, include/lib paths via $(EngineRootDir))
├── EngineRoot.props         # Defines $(EngineRootDir) macro — repo-root copy = $(SolutionDir)
├── Engine.sln               # Browse/verify/run solution (Engine StaticLibrary + Test exe)
├── Engine.vcxproj           # Engine StaticLibrary project for Engine.sln
└── GenerateProject.bat      # Unreal-style project generator (double-click)
```

## Technology Stack

- **OpenGL loader**: GLEW (`#include <GL/glew.h>`) — call `glewInit()` after `glfwMakeContextCurrent`
- **Windowing**: GLFW 3
- **Math**: GLM — use `#define GLM_SWIZZLE` before including if needed
- **Image loading**: stb_image (single-header, already in `include/stb_image.h`)
- **Model import**: Assimp (`assimp-vc143-mt`) for FBX/etc.; OBJ has a hand-written parser
- **Editor UI**: Dear ImGui (vendored in `Engine/Editor/External/imgui`, GLFW+OpenGL3 backends)
- **Language standard**: C++17 (set globally in `OpenglViewer.props`)
- **Platform**: Win32 (32-bit) only; the `.sln` and `.vcxproj` define only `Win32` configs
- **No GLAD** — this repo uses GLEW

## Path Resolution

**All `.vcxproj` and `OpenglViewer.props` engine-source paths use `$(EngineRootDir)`**, defined in a per-solution `EngineRoot.props` that is imported before `OpenglViewer.props`. Three configurations:

| Context | `EngineRoot.props` location | `EngineRootDir` value |
|---------|----------------------------|------------------------|
| `Engine.sln` (this repo) | repo root `EngineRoot.props` | `$(SolutionDir)` |
| Generated project (linked) | per-project `EngineRoot.props` written by GenerateProject | absolute path to engine repo |
| Generated project (packaged) | per-project `EngineRoot.props` rewritten by `Package.bat` | `$(SolutionDir)` |

`OutDir` still resolves through `$(SolutionDir)bin\` so each solution's exe and DLLs are co-located in that solution's own `bin\`. Either solution is meant to be opened from its own folder; do not open a `.vcxproj` directly without its `.sln`.

## VS Solution Explorer Filter Layout

Each project's `.vcxproj.filters` groups `main.cpp` under **Source Files** and the engine `.cpp`/`.h` under an **Engine** tree mirroring the `Engine\` subfolders (Framework, Core, World, Mesh, Rasterizer, Render, RayTracing, Acceleration, Serialization, Threading, Import, Editor, Light, Physics, Player). Keep new files filed under the matching module so Solution Explorer stays organized. **New engine files must be registered in `Engine.vcxproj` + `Test.vcxproj` + `Template/*.vcxproj` (and their `.filters`)** or they won't compile into the runnable exe.

## Engine Architecture

> **Two refactor waves.** (1) **Mesh-first**: the engine was rewritten from an analytic-surface ray tracer to a mesh renderer — `USurface`/`Sphere/Cube/PlaneSurface`, `UTilemap`, the `URayTracing` multi-pass shader assembler, and `RenderConfig.h` were **deleted**; geometry is now `UMesh` (triangles) shaded in three fixed render paths. (2) **Engine/editor polish** added the scene-graph (`USceneComponent`), serialization (`FArchive`/factories/`.world`/`.ini`), the `EditorEngine`↔`GameEngine` split with data-driven boot, and the GPU-RT two-level BVH. Docs/memories naming the old removed types are stale.

All engine source lives under `Engine/` and is compiled directly into each project (the `Engine.lib` StaticLibrary exists only for compile-error checking; `Test.exe` and generated projects compile the `.cpp` directly). `OpenglViewer.props` adds every `Engine\` subdirectory to the include path, so bare `#include "UMesh.h"` works everywhere. **GL baseline is 3.3** — all GPU passes use `#version 330` fragment shaders over fullscreen quads and `samplerBuffer` TBOs; no compute shaders / SSBO / GL 4.3 is assumed (the grading machine may not have it).

### Framework lifecycle (`Engine/Framework`)

`Engine` is the base runtime: it owns the GLFW window + GL context + main loop + resize handling, with virtual hooks an app overrides — `OnStartup()` (once, after GL is ready), `WorldSetting()` (build a `UWorld` + spawn actors), `Tick(dt)`, `Render()` (every frame), `OnResize()`. `Init(w,h,title)` then `Run(projPath)`. `Run()` flow: `OnStartup → WorldSetting → subsystems.InitAll → world.BeginPlay → loop{TickAll → world.Tick → Render} → world.EndPlay → subsystems.ShutdownAll`. No global state — the GLFW resize callback trampolines through the window user-pointer to a member. **Three `Engine` subclasses**: `MeshDemo` (`--demo`), `EditorEngine` (default, ImGui editor), and `GameEngine` (`--game`, no editor — loads a `.world` and runs it via `UWorldRenderer`).

`FProjectDescriptor` is the **data-driven boot**: a flat legacy `Key = Value` `.proj` plus the two-stage `LoadProject(.proj manifest)` + `LoadSettings(Setting/DefaultEngine.ini)` ([Display]/[Render]/[Startup]). A missing/garbage file must never crash (keep defaults). When `WorldSetting()` returns null and a `StartupWorld` is set, `Run()` loads `Content/<StartupWorld>.world`. `BuildManager` runs `msbuild` on a worker thread (path from `Config/Engine.ini`, gitignored) streaming output to the editor's Build Log. `USubsystemManager` owns a list of `USubsystem`s and drives Init (registration order) / Tick / Shutdown (reverse order); `Get<T>()` finds one by type.

### World & actors (`Engine/World`, `Engine/Core`)

`UWorld` is the runtime scene container (Unreal `UWorld` analogue): owns a `UScene` (`vector<AActor*>` + `vector<ALight*>` + `outputImage` float buffer + `renderMode`/`shadingModel`/`skyHDRI` + `UPostProcessFilter`), an `ACamera`, and a `UPhysicsWorld`, and drives the actor lifecycle (`BeginPlay` once → `Tick`: physics step then per-actor tick → `EndPlay`). `ACamera` uses the **FCG/Shirley convention** (see FTransform below) with `l,r,b,t,d` frustum fields.

**Scene graph (`USceneComponent`)**: every `AActor` owns a `rootComponent` (a `USceneComponent`) that holds the transform — `AActor` has **no** position field; `GetActorLocation()` reads the root's world matrix, `SetActorLocation()` writes its `relLocation`. A `USceneComponent` has a relative transform (loc/rot/scale, **rotation in DEGREES** — this GLM build is not `GLM_FORCE_RADIANS`), an `attachParent`, `children`, and a **dirty-flag-cached** world matrix (`world = parent.world * relative`; a move marks self + all descendants dirty, recomputed lazily). `UMeshComponent` and `LightComponent` attach **under** the root; **actor↔actor parenting** (editor outliner) attaches one actor's `rootComponent` under another's, so moving a parent moves its children and serialization/clone must preserve the link. Physics mutates positions through the setters so the dirty cache stays valid.

### Mesh & material (`Engine/Mesh`)

`UMesh` is the triangle-mesh asset (Unreal `StaticMesh` analogue): `vertices` (`Vertex` = position/normal/uv) + `indices` (3 per triangle) + a default `Material` (plus optional per-triangle material slots). Static generators **replace the old analytic surfaces**: `GenerateSphere(radius,segW,segH)`, `GenerateCube(halfExtents)`, `GeneratePlane(size)`. `GenerateSphere` reproduces the course `sphere_scene.cpp` vertex/index order **exactly** (default 32×16 → 450 verts / 868 tris) so rasterizer output matches the reference image pixel-for-pixel — do not "tidy" its ordering. `BuildBVH()` builds an optional `BVH`; `intersect(ray, worldMat, …)` is a CPU geometric query for **editor picking only** (not a render path — there is no CPU ray tracer). `SaveBinary`/`LoadBinary` are the `.mesh` format. **`UMesh::Resolve(ref)`** is the shared, cached asset resolver used by `.world` load and the editor: `ref` is either a procedural descriptor (`"Sphere r sw sh"` / `"Cube ..."` / `"Plane ..."`) or a content path it dispatches by extension (`.obj`→`UObjImporter`, `.fbx`→`UFbxImporter`, `.mesh`→`LoadBinary`). Meshes are shared across actors via `UMeshComponent` (whose `meshRef` string round-trips the descriptor/path).

`Material` (Phong/Blinn-Phong, moved out of the deleted `USurface`): `ka` ambient, `kd` diffuse, `ks` specular, `shininess` exponent, `km` mirror reflectance, `emissive`, GL `texture` id + CPU-side `texData/texWidth/texHeight/texChannels` (+ `diffuseTexPath`/`wrapMode`/`uvTiling`); `SampleDiffuse(uv)` is the CPU bilinear sampler (sRGB→linear) used by the raster path.

### Rasterizer (`Engine/Rasterizer`)

`FTransform` builds the model→view→proj→viewport stack **by hand** (no `glm::perspective`/`lookAt`) to honor the course FCG convention: **`n`/`f` are signed-negative z**, the projection bottom row is `[0 0 1 0]` (so `clip.w = z_eye`, negative for visible points). `MakeView` / `MakeProjFCG(l,r,b,t,n,f)` / `MakeViewport(nx,ny)` (origin bottom-left, depth → [0,1]).

`URasterizer` is a general-purpose software rasterizer (edge-function fill + depth test), reused by Q1, the hybrid renderer's primary-visibility pass, and the editor viewport. `DrawMesh(...)` → flat-shaded `UFrameBuffer`; `DrawMeshGBuffer(...)` → `UGBuffer` (perspective-correct world pos / normal / albedo / depth) for the hybrid path, with optional tile bounds (`cx0..cy1`) so disjoint tiles fill in parallel with no sync. Front-end clip/cull flags: `nearClip` (**accuracy** — clip in clip space before the w-divide flips a vertex with `w ≥ 0`), `frustumCull` / `backfaceCull` (**performance only — must not change the image**). `CullStats` counts per stage.

### Render paths (`Engine/Render`, `Engine/RayTracing`)

`URenderer` is the orchestrator that `Engine::Render()` calls. `ERenderMode` = `RasterOnly | GPURayTrace | Hybrid`; `Plan(mode)` returns a pure (GL-free) ordered `ERenderStage` list (used for dispatch tests), and `Render(world, mode)` executes it. It owns the rasterizer, frame/G buffers, and a `ThreadPool` for the lit shading pass (`multithread` toggles MT for the single-vs-MT test). The three paths:

| Path | How it works |
|------|--------------|
| **Rasterizer** (Q1, also the editor's default viewport + the lit `RasterShaded`) | CPU `URasterizer` → `UFrameBuffer` → `glDrawPixels` / viewport texture. Flat/Gouraud/Phong Blinn-Phong + gamma, or depth-debug. `RasterShaded` is **multithreaded**: scene triangles are partitioned across workers into per-thread framebuffers (setup once per triangle), then merged by nearest depth. |
| **GPU mesh ray trace** (`UMeshRayTracer`) | Fullscreen-quad `#version 330` fragment shader casts one camera ray/pixel, Möller-Trumbore, **two-level BVH (instancing)**: per-mesh BLAS built once in **mesh-local** space (cached by mesh identity), concatenated into shared TBOs; a per-instance record (inverse model, BLAS offsets, world AABB, albedo/km/texLayer) + a **TLAS** (BVH over instance AABBs) let a moving object rewrite only the small instance/TLAS buffer instead of rebuilding the whole tree. Blinn-Phong direct + hemisphere GI + sky (HDRI or gradient) + one mirror bounce. `UploadWorld` (editor/game), `SetLights`/`SetSky`/`SetGI`/`SetShadow`/`SetQuality` are per-frame. |
| **Hybrid** (`UHybridPass`) | CPU rasterizes primary visibility → `UGBuffer`, uploaded as float textures; a `#version 330` pass casts shadow/GI rays against a **single world-space BVH** (uploaded once per geometry change) and shades Blinn-Phong. G-buffer re-uploaded per frame. |

`RTShading.h` is the **single source of truth** for lighting: `skyColor`/`gradientSky`, `directLight` (lights + slope-scaled-bias shadow rays), `shadeSurface` (ambient/GI + direct), `tonemap`, GI helpers. It is concatenated into BOTH GPU passes. Two functions are **declared as prototypes here but defined per pass** so the two BVH layouts can differ: `occluded(...)` (Hybrid = single world BVH, GPU RT = two-level) and `giSampleRadiance(...)` (GPU RT path-traces `uGIBounces`, Hybrid = AO only). `URay.h` is the CPU ray struct. `UWorldRenderer` wraps all three modes to render a `UWorld` into the bound framebuffer (used by `GameEngine`; the editor has its own copy of the dispatch).

### Acceleration & threading

`BVH` (`Engine/Acceleration`) is built in **mesh-local** space (median split on the longest axis) with **stack-based** (non-recursive) slab traversal so it ports 1:1 to GLSL. 32-byte `BVHNode` packs leaf/inner via the sign of `rightOrTriCount` (the GPU-RT TLAS reuses the same node layout for its instance BVH). Consumed by `UMesh::intersect` (picking) and both GPU passes. `ThreadPool` (`Engine/Threading`, header-only) is a fixed worker pool; `ParallelForChunks(count, fn)` splits a range into ~`size()*4` contiguous chunks (small ranges run inline). Worker threads **never touch GL**.

### Serialization & assets (`Engine/Serialization`)

`FArchive` is a **bidirectional** archive — one `Serialize(ar)` method per type handles both save (`FSaveArchive`, accumulates `key = value`) and load (`FLoadArchive`, robust: missing/garbage → defaults, never crashes), so the two directions can't drift. `TFactory<Base>` + `REGISTER_ACTOR`/`REGISTER_COMPONENT` macros (Meyers-singleton tables) create types by `TypeName()` string. `FWorldSerializer` reads/writes `.world` (`[World]`/`[Camera]`/`[Actor]`(+`Parent`)/`[Component]`/`[Collision]`); on load it factory-creates actors/components, `Serialize`s them, attaches under the root, wires typed pointers (`mesh`, `ALight.lightComp`), and resolves the scene-graph hierarchy by parent name. `FIniFile` is a robust section parser (typed getters, comments) used for `Setting/*.ini`, `Config/Engine.ini`, and the render-settings profiles. **Serialized `TypeName()` strings are stable identifiers** — e.g. the light components serialize as `"PointLight"`/`"EnvLight"` even though the C++ classes were renamed `*Component`; don't change the strings or old `.world` files break.

### Geometry-upload caching

GPU geometry uploads are cached by a **signature** so static scenes don't rebuild every frame; during a drag only the cheap part re-uploads. **GPU RT (two-level)**: per-mesh BLAS is cached by mesh identity (built once); the concatenated BLAS TBOs + diffuse texture array are rebuilt only when `blasSig_` (mesh set + textures) changes; the small per-instance buffer + TLAS are rebuilt every `UploadWorld` call (cheap). The editor still gates the whole `UploadWorld` call by a `geomSig` (mesh + transform + albedo) so it only fires on change. **Hybrid**: the shadow-ray world-space triangle BVH is cached by geometry signature; the G-buffer raster + upload is camera-dependent and runs each frame. Mirror these patterns when adding GPU geometry uploads.

### Import (`Engine/Import`)

`UObjImporter` is a **hand-written** Wavefront `.obj` parser (no library): `Load` merges everything into one `UMesh` (de-dup by v/vt/vn triple, fan-triangulation, computed normals, 1-based & negative indices); `LoadMulti` is material-aware (splits per `usemtl`, reads `.mtl` Ka/Kd/Ks/Ns). `UFbxImporter` wraps **Assimp** (`assimp-vc143-mt.lib`/`.dll`) — only the binary parsing uses a library; the transform/raster/shading pipeline stays 100% in-engine (state this in the presentation). Both return owned `UMesh*` and an **empty result on missing/corrupt files, never a crash**. `LoadOptions` (FBX): `globalScale`, `flipUV`, `swapYZ`.

### Editor (`Engine/Editor`)

`EditorEngine : Engine` overlays a Dear ImGui editor (Toolbar / World Outliner / Details / Viewport / Content Browser / Render Settings) on a live `UWorld`. ImGui + **ImGuizmo** are vendored under `Engine/Editor/External/` and compiled in. Render modes 0=Rasterizer (lit `RasterShaded` → texture), 1=GPU RT, 2=Hybrid (the latter two render into an FBO → `ImGui::Image`). Key behaviors that span files:
- **Undo/PIE clones**: Undo snapshots and Play both use an in-memory lossless `CopyWorld`/`CloneActor` (NOT serialization) that shares `UMesh` assets and **preserves the scene-graph hierarchy** (src→dst remap) and concrete light types. View settings (camera, render mode, shading) are kept out of undo. PIE (`Play (Window)`) saves to `Content/__pie.world` and spawns `argv0 --game` as a separate process.
- **Outliner hierarchy**: drag an actor onto another to parent it (keeps world position; cycle-checked); tree collapse/expand; the gizmo converts edits into the parent-local transform.
- **Content/ on import**: importing a mesh/texture (browser / inspector slot / OS drag-drop / drag onto the viewport) copies the source into `Content/` (`CopyToContent`, incl. OBJ `.mtl` + its textures) and stores an in-`Content` `meshRef`/`diffuseTexPath` so a reopened project resolves it.
- **Render Settings** (`FRenderQuality`): two independent profiles, `editorRS_` (live viewport) and `gameRS_` (PIE/standalone), with `activeRS()` choosing by play state. Persisted to `Config/EditorSettings.ini` (`[Editor]`/`[Game]`) plus a standalone `Config/GameSettings.ini` that `UWorldRenderer` reads at game startup. Knobs: SSAA, ambient, GI samples/bounces/strength, reflection, shininess, shadow samples/softness.
- Left-click ray-picks an actor; RMB-fly + WASD + wheel-zoom drives the editor camera; double-click in the outliner focuses.

### Lights & physics (`Engine/Light`, `Engine/Physics`)

Lights are `LightComponent`-derived scene components attached under an `ALight`'s root: `PointLightComponent` (position = world transform) and `EnvironmentLightComponent` (drives hemisphere **GI** + sky gradient: horizon/zenith/exp). GPU shading is **Blinn-Phong for direct light + Unreal-Lumen-style cosine-hemisphere GI for environment light** (with `uGIBounces` color-bleed in GPU RT). Note these classes serialize as `"PointLight"`/`"EnvLight"` (see Serialization). Physics is **component-based**: `UPrimitiveComponent` → `UShapeComponent` → `USphereComponent` / `UBoxComponent`, stepped by `UPhysicsWorld` (velocity/gravity/collision); a body can be **Static** (`bSimulate=false`: collides but immovable) and colliders have a scalable size + local offset (drawn as a green wireframe when the collision component is selected).

### Player (`Engine/Player`)

`UPlayerController` possesses a `UPlayerCharacter`, drives pawn velocity from input, and poses an `ACamera` behind it each `Tick(window, dt)`. `SetupDefaultBindings()` wires WASD + Space (jump) + Left-Shift (sprint) + mouse-look; custom keys via `BindAction`/`BindAxis`/`BindMouseLook`; follow geometry via `cameraOffset` / `cameraDistance`.

## OpenglViewer.props Details

The shared property sheet (imported by all `.vcxproj` files):
- Sets `OutDir` → `$(SolutionDir)bin\`
- Sets `LanguageStandard` → `stdcpp17` (C++17 is required across the engine — structured bindings, `std::unique_ptr<BVH>` with incomplete type, etc.)
- Defines `GLFW_EXPOSE_NATIVE_WGL` (ImGui's GLFW backend includes `<GLFW/glfw3native.h>`, which needs a context-API macro)
- Adds `$(EngineRootDir)include` plus **every** `$(EngineRootDir)Engine\` subdirectory (incl. `Editor\External\imgui` + its `backends`) to include search paths, so bare `#include "Foo.h"` works
- Engine-source paths resolve through `$(EngineRootDir)`, defined in a sibling `EngineRoot.props` imported just before this one (works for linked + packaged generated projects and `Engine.sln`)
- Links `glew32.lib`, `freeglut.lib`, `glfw3dll.lib`, `opengl32.lib`, `glu32.lib`, **`assimp-vc143-mt.lib`** (FBX import) from `$(EngineRootDir)lib`

## Common Pitfalls

- **Stale architecture references**: The engine is now mesh-first. `USurface`/`Sphere/Cube/PlaneSurface`, `UTilemap`, `URayTracing` + its passes, and `RenderConfig.h` are **deleted**. There is **no CPU ray tracer** — render paths are Rasterizer / GPU mesh RT / Hybrid (`URenderer::ERenderMode`). Ignore older docs/memories naming the removed types.
- **Path errors in a generated project**: Open `EngineRoot.props` in the project root. If `<EngineRootDir>` is an absolute path (linked mode), make sure that path still exists. If it's `$(SolutionDir)` (packaged mode), make sure `Engine\`, `include\`, `lib\`, `OpenglViewer.props` are present next to the `.sln`. Re-run `GenerateProject.bat` or `Package.bat` to recover.
- **Assimp runtime**: FBX import needs `assimp-vc143-mt.dll` next to the exe (in `bin\`). It is tracked; don't delete it. A missing/corrupt model must return an empty result, never crash — preserve that contract in importer edits.
- **FCG matrix convention**: `FTransform`/`ACamera` use signed-negative `n`/`f` and a `[0 0 1 0]` projection bottom row, so `clip.w = z_eye` is **negative** for visible points and the perspective divide flips a vertex with `w ≥ 0`. This is why `nearClip` clips in clip space first. Do not swap in `glm::perspective`/`lookAt` — it breaks the course reference image.
- **`GenerateSphere` ordering is load-bearing**: it matches `sphere_scene.cpp` vertex/index order exactly for pixel-for-pixel reference comparison. Don't reorder it.
- **GPU passes are GL 3.3**: keep new GPU work as `#version 330` fragment shaders + `samplerBuffer` TBOs (not SSBO/compute) so it runs on the grading machine.
- **`textureLod`, never `texture()`, in ray-traced/looped shader code**: inside divergent control flow (GI loops, reflection, any ray bounce) implicit-LOD `texture()` needs screen-space derivatives that are **undefined there and crash some drivers** (this was a real GPU-RT EnvLight+HDRI crash). Use `textureLod(s, uv, 0.0)`. Top-level per-pixel sampling is fine.
- **Two GPU BVH layouts**: GPU RT is **two-level** (mesh-local BLAS + instance TLAS), Hybrid is **single world-space**. The shared `RTShading.h` therefore only *declares* `occluded()`/`giSampleRadiance()`; each pass *defines* them. Don't move a pass-specific definition back into the shared header.
- **Rotation is in DEGREES**: `USceneComponent`/`composeTRS` and `glm::rotate` here take degrees (no `GLM_FORCE_RADIANS`); for the gizmo projection use `glm::frustum(cam.l,cam.r,cam.b,cam.t,cam.d,far)`, not `glm::perspective(radians(fov))` (treated as degrees → ~1° fov).
- **C++17**: required engine-wide. The `/std:c++17` flag is set via `OpenglViewer.props` — do not remove it.
- **`stb_image.h`**: single-header image loader at `include/stb_image.h`, tracked. Do not delete it.
- **Standalone `*_test.cpp` are not in the VS build**: `Test.vcxproj` builds only `main.cpp`. Compile each `Test\*_test.cpp` individually with g++ (command in its header). Each has its own `main()`, so they cannot share a project.
- **Commit hygiene**: commit engine source under `Engine\` (and intentional `Test\*_test.cpp`); never commit `bin\` build output, `Test\Debug\` artifacts, or model files dropped into `bin\` for ad-hoc testing.
