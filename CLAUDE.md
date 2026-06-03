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
- **`--demo`** → mesh demo window, keys `1`=CPU raster silhouette, `2`=GPU mesh ray trace, `3`=depth view, `4`=hybrid (raster G-buffer + GPU shadow), `5`=imported OBJ cube, `6`=imported FBX.
- **`--fbxtest`** → headless FBX-import self-test (no GL window), prints PASS/FAIL gates, exits non-zero on failure.

### Standalone CPU unit tests (`Test\*_test.cpp`)
Each `Test\<area>_test.cpp` (rasterizer, bvh, physics, framework, threading, culling, hybrid, obj) is an **independent** GL-free self-test with its own `main()` and `[T#] … PASS|FAIL` gate lines. They are **not** part of `Test.vcxproj` (which only builds `main.cpp`); compile/run one at a time with g++ (msys2 ucrt64) — the exact command is in each file's header comment, e.g.:
```bash
g++ -std=c++17 -I include -I Engine/Mesh -I Engine/Rasterizer -I Engine/World -I Engine/RayTracing \
    Test/rasterizer_test.cpp Engine/Mesh/UMesh.cpp Engine/Rasterizer/FTransform.cpp \
    Engine/Rasterizer/URasterizer.cpp -o rasterizer_test && ./rasterizer_test
```

## Repository Structure

```
EngineDevelop/
├── Engine/                  # Tracked engine source — edit and commit freely
│   ├── Framework/           # Engine (window+loop+lifecycle), FProjectDescriptor (.proj), USubsystem(Manager)
│   ├── Core/                # UScene (actors/lights/outputImage), UPostProcessFilter
│   ├── World/               # UWorld (scene+camera+physics+lifecycle), AActor, ACamera
│   ├── Mesh/                # UMesh (geometry+generators+BVH+intersect), UMeshComponent, Vertex, Material
│   ├── Rasterizer/          # FTransform (FCG matrices), URasterizer, UFrameBuffer, UGBuffer
│   ├── Render/              # URenderer (mode orchestrator), UHybridPass (GPU shadow/shade)
│   ├── RayTracing/          # UMeshRayTracer (GPU mesh RT), RTShading.h, URay.h
│   ├── Acceleration/        # BVH (median-split, stack traversal, GLSL-portable)
│   ├── Threading/           # ThreadPool (header-only worker pool, ParallelForChunks)
│   ├── Import/              # UObjImporter (hand-written), UFbxImporter (Assimp)
│   ├── Editor/              # EditorEngine (Dear ImGui editor) + External/imgui (vendored)
│   ├── Light/               # ALight, PointLight, EnvironmentLight, LightComponent
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

Each project's `.vcxproj.filters` groups `main.cpp` under **Source Files** and the engine `.cpp`/`.h` under an **Engine** tree mirroring the `Engine\` subfolders (Framework, Core, World, Mesh, Rasterizer, Render, RayTracing, Acceleration, Threading, Import, Editor, Light, Physics, Player). Keep new files filed under the matching module so Solution Explorer stays organized.

## Engine Architecture

> **Major refactor (mesh-first).** The engine was rewritten from an analytic-surface ray tracer to a **mesh-first** renderer. The legacy `USurface`/`SphereSurface`/`CubeSurface`/`PlaneSurface`, `UTilemap`, the `URayTracing` multi-pass GPU shader assembler, and `RenderConfig.h` were **deleted**. Geometry is now `UMesh` (triangles); shading happens in three fixed render paths. If you find docs or memories referencing those old types, they are stale.

All engine source lives under `Engine/` and is compiled directly into each project (the `Engine.lib` StaticLibrary exists only for compile-error checking; `Test.exe` and generated projects compile the `.cpp` directly). `OpenglViewer.props` adds every `Engine\` subdirectory to the include path, so bare `#include "UMesh.h"` works everywhere. **GL baseline is 3.3** — all GPU passes use `#version 330` fragment shaders over fullscreen quads and `samplerBuffer` TBOs; no compute shaders / SSBO / GL 4.3 is assumed (the grading machine may not have it).

### Framework lifecycle (`Engine/Framework`)

`Engine` is the base runtime: it owns the GLFW window + GL context + main loop + resize handling, with virtual hooks an app overrides — `OnStartup()` (once, after GL is ready), `WorldSetting()` (build a `UWorld` + spawn actors), `Tick(dt)`, `Render()` (every frame), `OnResize()`. `Init(w,h,title)` then `Run(projPath)`. `Run()` flow: `OnStartup → WorldSetting → subsystems.InitAll → world.BeginPlay → loop{TickAll → world.Tick → Render} → world.EndPlay → subsystems.ShutdownAll`. No global state — the GLFW resize callback trampolines through the window user-pointer to a member. `MeshDemo` and `EditorEngine` both subclass `Engine`.

`FProjectDescriptor` parses a flat `Key = Value` `.proj` file (WindowTitle/Width/Height/RenderMode/StartupWorld); a missing/garbage file must never crash (keep defaults). `USubsystemManager` owns a list of `USubsystem`s and drives Init (registration order) / Tick / Shutdown (reverse order); `Get<T>()` finds one by type.

### World & actors (`Engine/World`, `Engine/Core`)

`UWorld` is the runtime scene container (Unreal `UWorld` analogue): owns a `UScene` (`vector<AActor*>` + `vector<ALight*>` + `outputImage` float buffer + `UPostProcessFilter`), an `ACamera`, and a `UPhysicsWorld`, and drives the actor lifecycle (`BeginPlay` once → `Tick`: physics step then per-actor tick → `EndPlay`). `AActor` holds a transform and components; meshes attach via `UMeshComponent`. `ACamera` uses the **FCG/Shirley convention** (see FTransform below) with `l,r,b,t,d` frustum fields.

### Mesh & material (`Engine/Mesh`)

`UMesh` is the triangle-mesh asset (Unreal `StaticMesh` analogue): `vertices` (`Vertex` = position/normal/uv) + `indices` (3 per triangle) + a default `Material`. Static generators **replace the old analytic surfaces**: `GenerateSphere(radius,segW,segH)`, `GenerateCube(halfExtents)`, `GeneratePlane(size)`. `GenerateSphere` reproduces the course `sphere_scene.cpp` vertex/index order **exactly** (default 32×16 → 450 verts / 868 tris) so rasterizer output matches the reference image pixel-for-pixel — do not "tidy" its ordering. `BuildBVH()` builds an optional `BVH`; `intersect(ray, worldMat, …)` is a CPU geometric query for **editor picking only** (not a render path — there is no CPU ray tracer). Meshes are shared across actors via `UMeshComponent`.

`Material` (Phong/Blinn-Phong, moved out of the deleted `USurface`): `ka` ambient, `kd` diffuse, `ks` specular, `shininess` exponent, `km` mirror reflectance, `emissive`, GL `texture` id + CPU-side `texData/texWidth/texHeight/texChannels` for sampling.

### Rasterizer (`Engine/Rasterizer`)

`FTransform` builds the model→view→proj→viewport stack **by hand** (no `glm::perspective`/`lookAt`) to honor the course FCG convention: **`n`/`f` are signed-negative z**, the projection bottom row is `[0 0 1 0]` (so `clip.w = z_eye`, negative for visible points). `MakeView` / `MakeProjFCG(l,r,b,t,n,f)` / `MakeViewport(nx,ny)` (origin bottom-left, depth → [0,1]).

`URasterizer` is a general-purpose software rasterizer (edge-function fill + depth test), reused by Q1, the hybrid renderer's primary-visibility pass, and the editor viewport. `DrawMesh(...)` → flat-shaded `UFrameBuffer`; `DrawMeshGBuffer(...)` → `UGBuffer` (perspective-correct world pos / normal / albedo / depth) for the hybrid path, with optional tile bounds (`cx0..cy1`) so disjoint tiles fill in parallel with no sync. Front-end clip/cull flags: `nearClip` (**accuracy** — clip in clip space before the w-divide flips a vertex with `w ≥ 0`), `frustumCull` / `backfaceCull` (**performance only — must not change the image**). `CullStats` counts per stage.

### Render paths (`Engine/Render`, `Engine/RayTracing`)

`URenderer` is the orchestrator that `Engine::Render()` calls. `ERenderMode` = `RasterOnly | GPURayTrace | Hybrid`; `Plan(mode)` returns a pure (GL-free) ordered `ERenderStage` list (used for dispatch tests), and `Render(world, mode)` executes it. It owns the rasterizer, frame/G buffers, and a `ThreadPool` for the lit shading pass (`multithread` toggles MT for the single-vs-MT test). The three paths:

| Path | How it works |
|------|--------------|
| **Rasterizer** (Q1) | CPU `URasterizer` → `UFrameBuffer` → `glDrawPixels`. Flat albedo or depth-debug view. |
| **GPU mesh ray trace** (`UMeshRayTracer`) | Fullscreen-quad `#version 330` fragment shader casts one camera ray/pixel, Möller-Trumbore against a **world-space triangle TBO** (`samplerBuffer`, 7 texels/tri), **BVH-accelerated** (node + leaf-index TBOs), shaded with one point light + sky gradient. `UploadMesh` (demo) / `UploadWorld` (editor, parallel mesh/model/albedo arrays). `SetLight` animates the light without re-uploading geometry. |
| **Hybrid** (`UHybridPass`) | CPU rasterizes primary visibility → `UGBuffer`, uploaded as float textures; a `#version 330` pass casts one **shadow ray/pixel** (Möller-Trumbore, BVH-accelerated) toward the light and shades Blinn-Phong. Upload scene triangles once per geometry change; G-buffer re-uploaded per frame. |

`RTShading.h` holds shared GLSL shading snippets; `URay.h` is the CPU ray struct.

### Acceleration & threading

`BVH` (`Engine/Acceleration`) is built in **mesh-local** space (median split on the longest axis) with **stack-based** (non-recursive) slab traversal so it ports 1:1 to GLSL. 32-byte `BVHNode` packs leaf/inner via the sign of `rightOrTriCount`. Consumed by `UMesh::intersect` (picking) and both GPU passes. `ThreadPool` (`Engine/Threading`, header-only) is a fixed worker pool; `ParallelForChunks(count, fn)` splits a range into ~`size()*4` contiguous chunks (small ranges run inline). Worker threads **never touch GL**.

### Geometry-upload caching

The editor caches BVH + triangle TBO uploads keyed by a geometry **signature** (mesh identity + world transform + albedo — camera-independent), rebuilding only when that signature changes rather than every frame. The hybrid G-buffer raster + upload is camera-dependent and still runs each frame; only its shadow-ray BVH is cached. Mirror this pattern (`*UploadSig_` / `*Uploaded_`) when adding GPU geometry uploads.

### Import (`Engine/Import`)

`UObjImporter` is a **hand-written** Wavefront `.obj` parser (no library): `Load` merges everything into one `UMesh` (de-dup by v/vt/vn triple, fan-triangulation, computed normals, 1-based & negative indices); `LoadMulti` is material-aware (splits per `usemtl`, reads `.mtl` Ka/Kd/Ks/Ns). `UFbxImporter` wraps **Assimp** (`assimp-vc143-mt.lib`/`.dll`) — only the binary parsing uses a library; the transform/raster/shading pipeline stays 100% in-engine (state this in the presentation). Both return owned `UMesh*` and an **empty result on missing/corrupt files, never a crash**. `LoadOptions` (FBX): `globalScale`, `flipUV`, `swapYZ`.

### Editor (`Engine/Editor`)

`EditorEngine : Engine` overlays a Dear ImGui editor (Toolbar / World Outliner / Details / Viewport / Content Browser) on a live `UWorld`. ImGui is **vendored** under `Engine/Editor/External/imgui` (+ GLFW/OpenGL3 backends) and compiled into the build. Render modes 0=Rasterizer (CPU `outputImage` → texture), 1=GPU RT, 2=Hybrid (the latter two render into an FBO → `ImGui::Image`). **PIE**: Play deep-copies `editorWorld_` into `pieWorld_` (sharing `UMesh` assets) and runs `BeginPlay`; Stop reverts. Left-click ray-picks an actor; RMB-fly + WASD drives the editor camera.

### Lights & physics (`Engine/Light`, `Engine/Physics`)

Lights (`PointLight`, `EnvironmentLight`, `LightComponent`) are preserved from the old engine; GPU shading should remain **Blinn-Phong for direct light and Unreal-style hemisphere sampling for environment/GI**, reusing the existing light info getters. Physics is now **component-based**: `UPrimitiveComponent` → `UShapeComponent` → `USphereComponent` / `UBoxComponent`, stepped by `UPhysicsWorld` (velocity/gravity/collision).

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
- **C++17**: required engine-wide. The `/std:c++17` flag is set via `OpenglViewer.props` — do not remove it.
- **`stb_image.h`**: single-header image loader at `include/stb_image.h`, tracked. Do not delete it.
- **Standalone `*_test.cpp` are not in the VS build**: `Test.vcxproj` builds only `main.cpp`. Compile each `Test\*_test.cpp` individually with g++ (command in its header). Each has its own `main()`, so they cannot share a project.
- **Commit hygiene**: commit engine source under `Engine\` (and intentional `Test\*_test.cpp`); never commit `bin\` build output, `Test\Debug\` artifacts, or model files dropped into `bin\` for ad-hoc testing.
