# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Purpose

This is a self-contained OpenGL engine template for Konkuk University Computer Graphics homework. After cloning, no additional library installation is required — all headers, `.lib` files, and `.dll` files are included.

## Workflows

There are **two** ways to work with this repo, depending on what you're doing:

### A) Make a standalone project (`GenerateProject.bat`)
Double-click `GenerateProject.bat` (Unreal-style — opens a cmd window, then a folder picker, then a name input box). It copies `Engine\`, `include\`, `lib\`, `bin\`, and `OpenglViewer.props` into `<picked-folder>\<name>\` and generates `<name>.sln` + `.vcxproj` + `.vcxproj.filters` + `main.cpp`. The result is **fully self-contained** — moving or deleting this engine repo will not break it. This is the only way to create a working project; use it both for homework and for personal experiments anywhere on disk.

Engine updates do **not** auto-propagate to projects created this way. Re-generate (or manually copy `Engine\`) to pick up changes.

### B) Explore / verify the engine itself (`Engine.sln`)
Open `Engine.sln` at the repo root. It contains a single `Engine` project (StaticLibrary) that compiles all of `Engine\**\*.cpp` to `bin\Engine.lib`. Use this when you want to navigate engine code with full IntelliSense and verify the engine still compiles after edits. The `.lib` itself isn't used by anything — the build step exists purely so compile errors surface immediately.

Engine improvements in `Engine\` can be committed back to this repo.

### Build
```bash
# Open the .sln in VS and press F5, or from a developer command prompt:
msbuild Engine.sln              /p:Configuration=Debug /p:Platform=Win32   # this repo
msbuild <generated>.sln         /p:Configuration=Debug /p:Platform=Win32   # external project
```
Output always goes to the solution's `bin\` (co-located with the runtime DLLs there).

## Repository Structure

```
MyFirstGameEngine/
├── Engine/                  # Tracked engine source — edit and commit freely
│   ├── Core/                # UScene, UPostProcessFilter
│   ├── World/               # AActor, ACamera, USurface, Sphere/Plane/CubeSurface, UTilemap
│   ├── Light/               # ALight, PointLight, EnvironmentLight, LightComponent
│   ├── Physics/             # PhysicalComponent, UPhysicsWorld
│   ├── Player/              # UPlayerCharacter (pawn), UPlayerController (input + camera follow)
│   └── RayTracing/          # URayTracing + pass classes + RenderConfig.h
├── Template/                # Scaffold (vcxproj + filters + main.cpp stub) — used by GenerateProject
├── Scripts/                 # GenerateProject.ps1 (project generator backend)
├── bin/                     # Runtime DLLs (tracked) + build output (gitignored)
├── include/                 # Third-party headers: GL/GLEW, GLFW, GLM, stb_image.h
├── lib/                     # Import libs: glew32, freeglut, glfw3, opengl32, glu32
├── OpenglViewer.props       # Shared MSBuild property sheet (C++17, include/lib paths)
├── Engine.sln               # Engine-only browse/verify solution (StaticLibrary)
├── Engine.vcxproj           # ...project file for Engine.sln
└── GenerateProject.bat      # Unreal-style standalone project generator (double-click)
```

## Technology Stack

- **OpenGL loader**: GLEW (`#include <GL/glew.h>`) — call `glewInit()` after `glfwMakeContextCurrent`
- **Windowing**: GLFW 3
- **Math**: GLM — use `#define GLM_SWIZZLE` before including if needed
- **Image loading**: stb_image (single-header, already in `include/stb_image.h`)
- **Language standard**: C++17 (set globally in `OpenglViewer.props`)
- **Platform**: Win32 (32-bit) only; the `.sln` and `.vcxproj` define only `Win32` configs
- **No GLAD** — this repo uses GLEW

## Path Resolution

**All `.vcxproj` and `OpenglViewer.props` paths use `$(SolutionDir)`**, which resolves to the directory containing the opened `.sln` file:

- `Engine.sln` (this repo) → `$(SolutionDir)` = repo root, so paths like `$(SolutionDir)Engine\...` work.
- `<generated>.sln` (external project) → `$(SolutionDir)` = the picked folder, which contains the copied `Engine\`, `include\`, `lib\`, `bin\`, and `OpenglViewer.props`, so the same paths work.

Either solution is meant to be opened from its own folder; do not open a `.vcxproj` directly without its `.sln`.

## VS Solution Explorer Filter Layout

Each project's `.vcxproj.filters` organizes files as:
```
<ProjectName>
├── Source Files
│   └── main.cpp
└── Engine
    ├── Core        (UScene, UPostProcessFilter)
    ├── World       (AActor, ACamera, Sphere/Plane/CubeSurface, UTilemap, ...)
    ├── Light       (ALight, PointLight, EnvironmentLight, ...)
    ├── Physics     (PhysicalComponent, UPhysicsWorld)
    ├── Player      (UPlayerCharacter, UPlayerController)
    └── RayTracing  (URayTracing + passes + RenderConfig.h)
```

## Engine Architecture

All engine source lives under `Engine/` and is compiled directly into each generated project (the StaticLibrary built by `Engine.sln` exists only for compile-error checking; nothing links against it). `OpenglViewer.props` adds all six `Engine\` subdirectories to the include path, so bare `#include "UScene.h"` style works everywhere.

| Module | Key Classes | Role |
|--------|-------------|------|
| `Engine/Core` | `UScene`, `UPostProcessFilter` | Scene holds `vector<AActor*>` + `vector<ALight*>` + `outputImage` pixel buffer + a `UPostProcessFilter filter` member |
| `Engine/World` | `AActor`, `ACamera`, `USurface`, `SphereSurface`, `PlaneSurface`, `CubeSurface`, `UTilemap` | Scene objects, camera, tilemap loader; `USurface` owns the `Material` (Phong + texture) |
| `Engine/Light` | `ALight`, `PointLight`, `EnvironmentLight`, `LightComponent` | Light sources; each implements `illuminate()` (CPU) and `getGLSLInfo()` (GPU) |
| `Engine/Physics` | `PhysicalComponent`, `UPhysicsWorld` | Velocity/gravity/collision attached to actors |
| `Engine/Player` | `UPlayerCharacter`, `UPlayerController` | Unreal-lite pawn + controller (WASD/mouse, third-person camera follow, action/axis binding) |
| `Engine/RayTracing` | `URayTracing`, `URay`, `URayTracingPass` (+ 5 concrete passes), `RenderConfig.h` | CPU ray tracer and GPU shader-assembled ray tracer |

### Player module

`UPlayerController` owns control logic: it possesses a `UPlayerCharacter`, drives the pawn's velocity from input, and poses an `ACamera` behind the pawn each `Tick(window, dt)`. `SetupDefaultBindings()` wires WASD + Space (jump) + Left-Shift (sprint) + mouse-look. Custom keys go through `BindAction`/`BindAxis`/`BindMouseLook`. Camera follow geometry is tunable via `cameraOffset` and `cameraDistance` fields.

### Tilemap

`UTilemap::Load(".tilemap path")` parses a text format with symbol table + grid placement (tile types: `Cube`, `Sphere`, `Player`, `Empty`; per-symbol material, physics, geometry overrides). `Spawn(scene)` instantiates actors and returns the spawned `UPlayerCharacter*` (or nullptr).

### CPU Rendering (`glDrawPixels`)

`URayTracing::Render(scene, camera, mode, settings)` fills `scene.outputImage` with RGB floats (row-major, origin bottom-left). The main loop passes this to `glDrawPixels`.

| Mode | Method | Description |
|------|--------|-------------|
| 0 | `TraceQ2` | Blinn-Phong shading + shadow rays. Uses `scene.Lights`. |
| 1 | `TraceQ3` | Phong + recursive mirror reflection (`km`) + uses `scene.Lights` (PointLight, EnvironmentLight) |

Output is tone-mapped (Reinhard) and gamma-corrected in `ApplyToneGamma()` based on `RenderSettings`. AA is performed in `TracePixelAA` (aaGrid×aaGrid samples per pixel, modes: 1=random, 2=stratified jitter, 3=Halton).

Apply the post-process filter chain (`scene.filter.Apply(...)`) after `Render()` if extra exposure/contrast/saturation/vignette is desired.

### GPU Rendering (fullscreen quad)

`URayTracing::Init(scene)` dynamically assembles a GLSL fragment shader by concatenating the `getGLSL()` output of five passes (all derive from `URayTracingPass`):

| Pass | GLSL contribution | Per-frame upload |
|------|-------------------|------------------|
| `GeometryPass` | Surface-type uniforms, per-type hit functions, `findClosest()`, `occluded()`, `getNormal()` | Sphere/plane arrays, cube UBO (`CubeBlock`, up to 256 cubes), brick texture |
| `MaterialPass` | `getMaterial()`, `getDiffuse()`, texture sampling | Material parameter arrays |
| `DirectLightPass` | Per-light constants/functions for PointLights, `skyColor()` sky gradient, `shadeDirect()` dispatcher | PointLight positions/colors/intensities |
| `IndirectLightPass` | Per-light indirect functions (EnvironmentLight hemisphere sampling), `shadeIndirect()` | EnvironmentLight sky params |
| `PostProcessPass` | `_hash`/`_halton` helpers, `trace()` reflection loop (`MAX_DEPTH=5`), `main()` with AA sampling + Reinhard tone map + gamma. **All AA/gamma values come from `RenderConfig.h` injected into the shader preamble at compile time.** | none |

`RenderFrame(scene, cam, settings)` walks the passes calling `uploadUniforms()` and draws.

### RenderConfig.h — compile-time render constants

`Engine/RayTracing/RenderConfig.h` defines the AA and gamma defaults baked into the GPU shader at `Init()` time and used as the default `RenderSettings` for CPU rendering. Change values here and rebuild to switch behavior without touching scene code.

```cpp
#define AA_ENABLE  1              // 0=off, 1=on
#define AA_GRID    2              // total samples = AA_GRID^2  (2→4spp, 3→9spp, 4→16spp)
#define AA_MODE    2              // 1=random, 2=stratified jitter, 3=Halton
#define GAMMA_ENABLE  1
#define GAMMA_VALUE   2.2f
```

`RenderSettings` (passed to `Render()` / `RenderFrame()`) lets per-homework code override at runtime — e.g. toggle AA via a key press without rebuilding.

### Material struct (`USurface.h`)

```cpp
struct Material {
    glm::vec3 ka        = vec3(0.2f);  // ambient
    glm::vec3 kd        = vec3(1.0f);  // diffuse
    glm::vec3 ks        = vec3(0.0f);  // specular
    float     shininess = 0.0f;        // Phong exponent
    glm::vec3 km        = vec3(0.0f);  // mirror reflectance (0=matte, 1=perfect mirror)
    glm::vec3 emissive  = vec3(0.0f);  // self-emission
    unsigned int texture = 0;          // GL texture ID (0 = none)
    // + CPU-side texData, texWidth, texHeight, texChannels for ray tracer sampling
};
```

### Surface Types

| Class | typeId | Key fields | Note |
|-------|--------|-----------|------|
| `SphereSurface` | 0 | `radius` | Position from `owner->position` |
| `CubeSurface` | 1 | `halfVec` (per-axis half-extents) | GPU data via UBO |
| `PlaneSurface` | 2 | — | Always horizontal (normal = +Y); height from `owner->position.y` |

### Light Types

| Class | Behavior |
|-------|----------|
| `PointLight(pos, color, intensity)` | Phong diffuse+specular; 9-sample soft shadow (1 center + 8 disk samples, radius `SOFT_SHADOW_RADIUS=0.7`) |
| `EnvironmentLight(color, intensity)` | Hemisphere sampling (Fibonacci spiral, `ENV_LIGHT_SAMPLES=4`, `GI_BOUNCE_DEPTH=1`); 1-bounce GI via `TraceQ3` recursion. Exposes `horizonColor`/`zenithColor`/`skyExp` sky-gradient fields and a `static applyTimeOfDay(light, tod)` helper that re-tints the sky/intensity for `tod ∈ [-10, +10]` (midnight → noon). |

### UPostProcessFilter (CPU only)

`Engine/Core/UPostProcessFilter.h` — applied to `scene.outputImage` after `Render()`, before `glDrawPixels`. Chain order: exposure → contrast → saturation → gamma → vignette. Each field defaults to identity. This is independent of the gamma already applied inside `ApplyToneGamma()`; set `filter.gamma = 0` to disable double-gamma if you also use `RenderSettings.enableGamma`.

## OpenglViewer.props Details

The shared property sheet (imported by all `.vcxproj` files):
- Sets `OutDir` → `$(SolutionDir)bin\`
- Sets `LanguageStandard` → `stdcpp17` (C++17 required for structured bindings in `URayTracing.cpp`)
- Adds `$(SolutionDir)include` and all six `Engine\` subdirectories (Core, World, Light, Physics, Player, RayTracing) to include search paths
- Links `glew32.lib`, `freeglut.lib`, `glfw3.lib`, `glfw3dll.lib`, `opengl32.lib`, `glu32.lib`

## Common Pitfalls

- **Path errors in a generated project**: If includes or engine `.cpp` files cannot be found, the generated folder is probably missing `OpenglViewer.props`, `Engine\`, `include\`, or `lib\`. Re-run `GenerateProject.bat`.
- **C++17**: `URayTracing.cpp` uses structured bindings (`auto& [k, v]`). The `/std:c++17` flag is set via `OpenglViewer.props` — do not remove it.
- **`stb_image.h`**: Required by `USurface.cpp` (`SetTexture`). The file is at `include/stb_image.h` and is tracked. Do not delete it.
- **GPU mode after resize**: Resizing the window while in GPU mode falls back to CPU; re-press `G` to reinitialize the shader.
- **CPU vs GPU sync**: `RenderConfig.h` is baked into the GPU shader at `Init()` time. Changing `RenderConfig.h` requires a rebuild; runtime toggles must use `RenderSettings` (CPU honors them; GPU only honors `RenderSettings` for what the shader can override at uniform level — AA grid and mode are compile-time on GPU).
- **Adding a new pass**: derive from `URayTracingPass`, implement `getGLSL()` (and `uploadUniforms()` if needed), then instantiate it in `URayTracing::Init()` and add to the shader assembly + per-frame upload loop in `URayTracing.cpp`.
