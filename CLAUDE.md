# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Purpose

This is a self-contained OpenGL engine template for Konkuk University Computer Graphics homework. After cloning, no additional library installation is required — all headers, `.lib` files, and `.dll` files are included.

## Workflow

**Start a new homework project:**
```powershell
.\new-project.ps1 HW4
```
This creates `Source\HW4\` with a pre-configured `HW4.vcxproj`, `HW4.sln`, and `main.cpp` stub. Open `Source\HW4\HW4.sln` in VS and start coding.

`Source\` is gitignored — homework code stays local. Engine improvements in `Engine\` can be committed.

**Build:**
```bash
# From repo root, builds Template project
msbuild OpenglViewer.sln /p:Configuration=Debug /p:Platform=Win32
```
Output always goes to `bin\` (co-located with the runtime DLLs there).

## Repository Structure

```
MyFirstGameEngine/
├── Engine/          # Tracked engine source — edit and commit freely
├── Template/        # Reference project (Template.vcxproj + main.cpp stub)
├── Source/          # GITIGNORED — your homework projects live here
├── bin/             # Runtime DLLs (tracked) + build output (gitignored)
├── include/         # Third-party headers: GL/GLEW, GLFW, GLM
├── lib/             # Import libs: glew32, freeglut, glfw3, opengl32, glu32
├── OpenglViewer.props  # Shared MSBuild property sheet (include/lib paths)
├── OpenglViewer.sln    # Solution — currently contains only Template project
└── new-project.ps1     # Scaffold script
```

## Technology Stack

- **OpenGL loader**: GLEW (`#include <GL/glew.h>`) — call `glewInit()` after `glfwMakeContextCurrent`
- **Windowing**: GLFW 3
- **Math**: GLM — use `#define GLM_SWIZZLE` before including if needed
- **Platform**: Win32 (32-bit) only; the `.sln` and `.vcxproj` define only `Win32` configs
- **No GLAD** — this repo uses GLEW

## Engine Architecture

All engine source lives under `Engine/` and is compiled directly into each Source project (no separate static lib). The `OpenglViewer.props` property sheet adds all Engine subdirectories to the include path, so bare `#include "UScene.h"` style works.

| Module | Key Classes | Role |
|--------|-------------|------|
| `Engine/Core` | `UScene` | Scene container: holds `vector<AActor*>` + `vector<ALight*>`, plus `outputImage` pixel buffer |
| `Engine/World` | `AActor`, `ACamera`, `USurface`, `SphereSurface`, `PlaneSurface`, `CubeSurface` | Scene objects and camera; `USurface` owns the `Material` (Phong coefficients, texture) |
| `Engine/Light` | `ALight`, `PointLight`, `EnvironmentLight`, `LightComponent` | Light sources; `ALight::illuminate()` is overridden per light type |
| `Engine/Physics` | `PhysicalComponent`, `UPhysicsWorld` | Physics simulation attached to actors |
| `Engine/RayTracing` | `URayTracing`, `URay`, `IntersectionPass`, `LightingPass` | CPU ray tracer (`Render()`) and GPU shader-assembled ray tracer (`Init/RenderFrame/Cleanup`) |

### CPU rendering pattern (glDrawPixels)

`URayTracing::Render(scene, camera, mode)` fills `scene.outputImage` with RGB floats. The main loop passes this to `glDrawPixels`. Mode 0 = intersection-only (white/black), mode 1 = Phong + shadows, mode 2+ = reflections.

### GPU rendering pattern

`URayTracing::Init(scene)` assembles a GLSL fragment shader dynamically from each surface's `SurfaceGLSLInfo` (type-specific intersection and shading code), compiles it, and builds a fullscreen quad. `RenderFrame(scene, cam)` uploads uniforms and draws.

### Material struct (in USurface.h)

```cpp
struct Material {
    glm::vec3 ka, kd, ks;  // Phong ambient/diffuse/specular
    float     shininess;
    glm::vec3 km;           // mirror reflectance (0=matte, 1=perfect mirror)
    glm::vec3 emissive;
    // + texture fields
};
```

## OpenglViewer.props Details

All paths use `$(SolutionDir)` so they resolve correctly regardless of where a project sits under `Source\`. The property sheet:
- Sets `OutDir` → `$(SolutionDir)bin\`
- Adds `$(SolutionDir)include` and all five `Engine\` subdirectories to include search paths
- Links `glew32.lib`, `freeglut.lib`, `glfw3.lib`, `glfw3dll.lib`, `opengl32.lib`, `glu32.lib`
