# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Purpose

This is a self-contained OpenGL engine template for Konkuk University Computer Graphics homework. After cloning, no additional library installation is required — all headers, `.lib` files, and `.dll` files are included.

## Workflow

**Start a new homework project:**
```powershell
.\new-project.ps1 HW4
```

This does three things:
1. Creates `Source\HW4\` with `HW4.vcxproj`, `HW4.vcxproj.filters`, and `main.cpp`
2. Adds the project to `OpenglViewer.sln` (no standalone `.sln` is created)
3. Generates filters so VS Solution Explorer shows Engine files organized by subfolder

Then open `OpenglViewer.sln` in Visual Studio and reload when prompted. `HW4` will appear immediately with Engine files in filters.

`Source\` is gitignored — homework code stays local. Engine improvements in `Engine\` can be committed.

**Build:**
```bash
# From repo root — open OpenglViewer.sln in VS and press F5, or:
msbuild OpenglViewer.sln /p:Configuration=Debug /p:Platform=Win32
```
Output always goes to `bin\` (co-located with the runtime DLLs there).

## Repository Structure

```
MyFirstGameEngineProject/
├── Engine/                  # Tracked engine source — edit and commit freely
│   ├── Core/                # UScene
│   ├── World/               # AActor, ACamera, USurface, Sphere/Plane/CubeSurface
│   ├── Light/               # ALight, PointLight, EnvironmentLight, LightComponent
│   ├── Physics/             # PhysicalComponent, UPhysicsWorld
│   └── RayTracing/          # URayTracing, URay, IntersectionPass, LightingPass
├── Template/                # Scaffold template (vcxproj + filters + main.cpp stub)
├── Source/                  # GITIGNORED — homework projects live here
│   └── TestScene/           # Example test project (exercises all engine features)
├── bin/                     # Runtime DLLs (tracked) + build output (gitignored)
├── include/                 # Third-party headers: GL/GLEW, GLFW, GLM, stb_image.h
├── lib/                     # Import libs: glew32, freeglut, glfw3, opengl32, glu32
├── OpenglViewer.props       # Shared MSBuild property sheet (C++17, include/lib paths)
├── OpenglViewer.sln         # Main solution — add all projects here via new-project.ps1
└── new-project.ps1          # Scaffold script — adds to OpenglViewer.sln automatically
```

## Technology Stack

- **OpenGL loader**: GLEW (`#include <GL/glew.h>`) — call `glewInit()` after `glfwMakeContextCurrent`
- **Windowing**: GLFW 3
- **Math**: GLM — use `#define GLM_SWIZZLE` before including if needed
- **Image loading**: stb_image (single-header, already in `include/stb_image.h`)
- **Language standard**: C++17 (set globally in `OpenglViewer.props`)
- **Platform**: Win32 (32-bit) only; the `.sln` and `.vcxproj` define only `Win32` configs
- **No GLAD** — this repo uses GLEW

## Path Resolution — Critical Rule

**All `.vcxproj` and `OpenglViewer.props` paths use `$(SolutionDir)`.**

`$(SolutionDir)` resolves to the directory containing the opened `.sln` file.
- `OpenglViewer.sln` is at the **repo root** → `$(SolutionDir)` = repo root ✅
- Opening a standalone `.sln` from inside `Source\<Name>\` → `$(SolutionDir)` = `Source\<Name>\` ❌

**Always open `OpenglViewer.sln` — never open a project's own `.sln` file directly.**
The `new-project.ps1` no longer creates standalone `.sln` files for this reason.

## VS Solution Explorer Filter Layout

Each project's `.vcxproj.filters` organizes files as:
```
<ProjectName>
├── Source Files
│   └── main.cpp
└── Engine
    ├── Core        (UScene.cpp)
    ├── World       (AActor, ACamera, Sphere/Plane/CubeSurface, ...)
    ├── Light       (ALight, PointLight, EnvironmentLight, ...)
    ├── Physics     (PhysicalComponent, UPhysicsWorld)
    └── RayTracing  (URayTracing.cpp)
```

## Engine Architecture

All engine source lives under `Engine/` and is compiled directly into each Source project (no separate static lib). `OpenglViewer.props` adds all Engine subdirectories to the include path, so bare `#include "UScene.h"` style works everywhere.

| Module | Key Classes | Role |
|--------|-------------|------|
| `Engine/Core` | `UScene` | Scene container: holds `vector<AActor*>` + `vector<ALight*>`, plus `outputImage` pixel buffer |
| `Engine/World` | `AActor`, `ACamera`, `USurface`, `SphereSurface`, `PlaneSurface`, `CubeSurface` | Scene objects and camera; `USurface` owns the `Material` (Phong coefficients, texture) |
| `Engine/Light` | `ALight`, `PointLight`, `EnvironmentLight`, `LightComponent` | Light sources; each type implements `illuminate()` and `getGLSLInfo()` |
| `Engine/Physics` | `PhysicalComponent`, `UPhysicsWorld` | Physics simulation attached to actors |
| `Engine/RayTracing` | `URayTracing`, `URay`, `IntersectionPass`, `LightingPass` | CPU ray tracer (`Render()`) and GPU shader-assembled ray tracer (`Init/RenderFrame/Cleanup`) |

### CPU Rendering (`glDrawPixels`)

`URayTracing::Render(scene, camera, mode)` fills `scene.outputImage` with RGB floats (row-major, origin bottom-left). The main loop passes this to `glDrawPixels`.

| Mode | Method | Description |
|------|--------|-------------|
| 0 | `TraceQ1` | Intersection test only — white(hit) / black(miss) |
| 1 | `TraceQ2` | Phong shading + shadow rays. **Uses a hardcoded light at `(-4,4,-3)` — ignores `scene.Lights`** |
| 2 | `TraceQ3` | Phong + recursive mirror reflection (`km`) + uses `scene.Lights` (PointLight, EnvironmentLight) |

### GPU Rendering (fullscreen quad)

`URayTracing::Init(scene)` dynamically assembles a GLSL fragment shader:
- `IntersectionPass::getGLSL()` — generates per-type hit functions, `findClosest()`, `getNormal()`, `getDiffuse()`, property getters
- `LightingPass::getGLSL()` — generates per-light shading functions, `shade()`, `skyColor()`
- Fixed `TRACE_GLSL` loop (reflection loop up to `MAX_DEPTH=5`)

`RenderFrame(scene, cam)` uploads uniforms and draws. Cube data goes through a UBO (`CubeBlock`); spheres/planes use per-uniform uploads.

### Material struct (`USurface.h`)

```cpp
struct Material {
    glm::vec3 ka        = vec3(0.2f);  // ambient
    glm::vec3 kd        = vec3(1.0f);  // diffuse
    glm::vec3 ks        = vec3(0.0f);  // specular
    float     shininess = 0.0f;        // Phong exponent
    glm::vec3 km        = vec3(0.0f);  // mirror reflectance (0=matte, 1=perfect mirror)
    glm::vec3 emissive  = vec3(0.0f);  // self-emission (used in mode 2 and GPU)
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
| `EnvironmentLight(color, intensity)` | Hemisphere sampling (Fibonacci spiral, `ENV_LIGHT_SAMPLES=4`); 1-bounce GI via `TraceQ3` recursion |

## OpenglViewer.props Details

The shared property sheet (imported by all `.vcxproj` files):
- Sets `OutDir` → `$(SolutionDir)bin\`
- Sets `LanguageStandard` → `stdcpp17` (C++17 required for structured bindings in `URayTracing.cpp`)
- Adds `$(SolutionDir)include` and all five `Engine\` subdirectories to include search paths
- Links `glew32.lib`, `freeglut.lib`, `glfw3.lib`, `glfw3dll.lib`, `opengl32.lib`, `glu32.lib`

## Common Pitfalls

- **Path errors**: If includes or engine `.cpp` files are not found, you likely opened a project's own `.sln` instead of `OpenglViewer.sln`. Always use the root solution.
- **C++17**: `URayTracing.cpp` uses structured bindings (`auto& [k, v]`). The `/std:c++17` flag is set via `OpenglViewer.props` — do not remove it.
- **`stb_image.h`**: Required by `USurface.cpp` (`SetTexture`). The file is at `include/stb_image.h` and is tracked. Do not delete it.
- **Mode 1 vs Mode 2**: CPU mode 1 (`TraceQ2`) has a hardcoded light and does not use `scene.Lights`. If scene lighting looks wrong in mode 1, this is expected — use mode 2 or GPU mode for scene-defined lights.
- **GPU mode after resize**: Resizing the window while in GPU mode falls back to CPU; re-press `G` to reinitialize the shader.
