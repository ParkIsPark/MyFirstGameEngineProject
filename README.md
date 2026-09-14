# MyFirstGameEngine

An OpenGL engine and editor for Konkuk University Computer Graphics coursework. Editor and Game share one hardware renderer: indexed mesh draws create primary visibility in a GPU G-buffer, raster lighting shades it, and a composite writes the viewport or game target. Ray tracing is off by default. Enabling it adds optional ray-traced shadows, GI, and reflections to the same raster primary image.

The supported build is Windows, Visual Studio 2022, C++17, **Win32 (32-bit)**. Install the VS C++ workload and Windows SDK. Dependency headers, libraries, runtime DLLs, and upstream Lua 5.4.9 are vendored; no separate dependency package installation is needed. A driver providing **OpenGL 3.3 compatibility** is the minimum; **4.3 compatibility** is preferred for Compute. Context creation tries 4.3 then 3.3; an actual context below 3.3 is a fatal startup error.

## Build and run

Open `Engine.sln`, select **Test**, **Debug | Win32**, and press F5. From a VS developer PowerShell:

```powershell
$env:_CL_='/FS'
msbuild Engine.sln /t:Build /p:Configuration=Debug /p:Platform=Win32 /m:1 /nr:false
.\bin\Test.exe
# Standalone runtime:
.\bin\Test.exe --game Content/EditorWorld.world
```

The editor provides an Outliner, Details, Content Browser and viewport. Click to select; hold RMB with WASD/QE to fly; scroll to move the camera; double-click an Outliner Actor to focus it. Move/Rotate/Scale gizmos edit transforms. Ctrl+S saves, Ctrl+Z/Y undo/redo, Ctrl+C/V copy/paste, and Delete removes the selection. Parenting preserves world position and rejects cycles.

**Add / Cube** creates a cube eight units in front of the camera. It renders through the shared hardware path. Arbitrary viewport-surface click-to-place coordinates are **not implemented by this renderer migration**; viewport left-click currently selects an Actor. This distinction matters for a literal click-position assignment rubric. Cube rotation in the assignment/demo is C++ `AActor::Tick` behavior, not a Lua transform binding.

## Rendering

The toolbar offers Flat/Gouraud/Phong shading and named ray features. Hardware raster primary visibility is mandatory. RT master off plans no ray pass and performs no ray factory, initialization, allocation, upload, draw, dispatch, or barrier work. RT master on adds only selected secondary effects.

| Backend | Behavior |
|---|---|
| Auto | Compute on a capable 4.3 context; otherwise Compatible with a recorded reason. Compute initialization failure falls back to Compatible. |
| Compatible | Explicit OpenGL 3.3 fragment backend, even on a 4.3-capable driver. |
| Compute | Requires the 4.3 Compute/SSBO entry points. Unavailable or failed initialization disables ray effects and retains raster output; forced Compute does not silently select Compatible. |

Materials support diffuse/specular/emissive values, shininess, diffuse textures and component UV tiling. Multiple point lights and environment/HDRI contribute to lighting; mirror response uses optional ray reflections. The mesh cache is keyed by immutable asset identity and explicit geometry revision: 1, 100 or 1000 Actors sharing one cube upload one geometry, then submit one indexed draw per cube. Unchanged frames and transform edits never reupload its vertices or indices. Editing geometry requires `MarkGeometryDirty`/`FinalizeGeometry`.

Render Settings keeps separate Editor and Game quality profiles in `Config/EditorSettings.ini` and `Config/GameSettings.ini`. **Play** runs a cloned world in the editor; **Stop** ends it. **Play (Window)** saves a temporary world and starts the current Editor executable with `--game`, which still uses GameEngine and the shared renderer.

Developer Settings is an Editor-only diagnostic panel. Its local, gitignored `Config/DeveloperSettings.ini` stores `[Rendering]` keys `LegacyOverride=None`, `ShowDeprecatedFeatures=true`, and `ShowExperimentalWarnings=true`. Deprecated/Experimental badges and warnings describe feature status. The two explicit legacy overrides are `SoftwareRasterizer` and `PureGPURayTracer`. Old CPU software raster, whole-frame GPU ray tracing, and the CPU-G-buffer `UHybridPass` are educational/regression implementations, not normal render choices. Developer Settings is excluded from project settings, worlds and packaged Game behavior.

See [the renderer guide](docs/rendering/hardware-raster-ray-effects.md) for resource counters, fallback troubleshooting, complete Editor/Game call stacks and the file-by-file assignment code map.

## Projects, assets and Lua

Run `GenerateProject.bat`, or:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/GenerateProject.ps1 -Parent C:/Projects -Name MyGame
```

Generated projects use **Editor | Win32** and **Game | Win32**. Their independent artifacts are `bin/MyGame-Editor.exe` and `bin/MyGame-Game.exe`; both remain alongside runtime DLLs, so alternating incremental builds cannot overwrite the other role. `EngineRoot.props` initially links back to this repository. Run the generated `Package.bat` to copy engine/dependency sources and switch to a self-contained project. Packaging preserves safe `.lua` assets beneath `Content/Scripts` and excludes local Developer Settings. Set the startup world in File / Project Settings (`Setting/DefaultEngine.ini`).

Import OBJ/MTL, FBX (Assimp), or engine `.mesh` assets through the Content Browser, Details, or OS drag/drop. Imports copy assets and supported sidecars into `Content`. Shared `.material` assets can be assigned to multiple objects; saving an imported MTL creates a `.material` file. Point/Environment lights and Sphere/Box colliders are Actor components.

World saves now use **format 3** with named `[RenderFeatures]`. Legacy format 1 and 2 files migrate on load and re-save as format 3; old numeric renderer settings are migration input only. ScriptComponent order, enabled state and paths survive this migration.

Attach Lua through Details / Add Component / Script Component, then assign a `.lua` from `Content/Scripts`. Each attachment has its own environment; callbacks run only during Play. The current public API is **`Engine.Log("message")` only**. There are no Lua Actor/transform/spawn/input/render APIs or hot reload. See [Lua Actor scripting](docs/scripting/lua-actor-scripting.md) and [upstream Lua provenance](ThirdParty/Lua/README.md).

## Verification and authorship

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Test/ScriptPackagingTest.ps1
.\bin\Test.exe --meshrevisiontest
.\bin\Test.exe --deprecated-raytracer-lifecycle-selftest
.\bin\Test.exe --hw-raster-selftest=hardware.ppm
.\bin\Test.exe --raster-lighting-selftest=lighting.ppm
.\bin\Test.exe --ray-effects-selftest=rays.ppm
.\bin\Test.exe --render-performance-selftest
```

These bounded gates cover import/revision, migration, Lua, deprecated isolation, real hardware shading, secondary ray effects, 1/100/1000 shared cubes, and production Editor/Game routes. The PPM gates deliberately read test images; normal rendering does not. `--demo` and `--hw6` are interactive educational/regression viewers of earlier coursework.

Project-authored engine/integration work lives in the framework, world/components, rendering, import adapters, serialization, editor integration, scripting integration and tests. This is a code responsibility map, not a claim that every file or dependency was authored here. `ThirdParty`, `include`, libraries/DLLs, Dear ImGui/ImGuizmo, GLEW/GLFW/GLM/Assimp, stb and upstream Lua 5.4.9 retain their upstream authorship and licenses. See [CLAUDE.md](CLAUDE.md) for development conventions.
