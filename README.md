# MyFirstGameEngine

A self-contained, Unreal-flavored OpenGL engine for Konkuk University Computer Graphics coursework. It ships an ImGui **editor**, a standalone **game runtime**, and three render paths — a CPU **software rasterizer** (the graded HW deliverable), a **GPU mesh ray tracer**, and a **hybrid** renderer.

Everything needed to build is vendored in the repo (`include/`, `lib/`, `bin/*.dll`). After cloning, **no library install is required** — open a solution and press F5.

- **Platform:** Windows, Visual Studio 2022, **Win32 (32-bit)** only
- **GL baseline:** OpenGL 3.3 (GLEW + GLFW + GLM)
- **Language:** C++17

---

## Quick start

1. Open `Engine.sln` at the repo root in Visual Studio 2022.
2. Set **Test** as the startup project, choose **Debug | Win32**, press **F5**.
3. The **editor** opens. Left-click to select, hold **RMB + WASD/QE** to fly, scroll to zoom.

From a developer command prompt instead:

```powershell
msbuild Engine.sln /p:Configuration=Debug /p:Platform=Win32
bin\Test.exe
```

### Run modes (`Test.exe`)

| Command | What it runs |
|---------|--------------|
| `Test.exe` (no args) | **Editor** (default) |
| `Test.exe --game <world.world>` | **Standalone game**: load a `.world` and play it (defaults to `Content/EditorWorld.world`) |
| `Test.exe --demo` | Mesh demo window — keys `1`=raster `2`=GPU RT `3`=depth `4`=hybrid `5`=OBJ `6`=FBX |
| `Test.exe --hw6` | HW6 viewer — keys `1`=Flat `2`=Gouraud `3`=Phong on the reference sphere |
| `Test.exe --fbxtest` | Headless FBX-import self-test (no window; PASS/FAIL gates) |

---

## Using the editor

The editor is laid out as **Toolbar / World Outliner / Viewport / Content Browser / Details**, with a **Render Settings** window from the toolbar.

### Camera & selection

| Input | Action |
|-------|--------|
| **Left-click** | Ray-pick an actor in the viewport |
| **RMB (hold) + W A S D** | Fly the camera; **Q/E** = down/up |
| **Mouse wheel** | Zoom (dolly); hold **Shift** for faster |
| **Double-click** an actor in the Outliner | Focus the camera on it |

### Transform gizmo

Select an actor, then use the toolbar **Move / Rotate / Scale** buttons (and **Local / World** space) to drag the gizmo. Edits write back to the actor's transform; for a parented actor the gizmo edits its **local** transform.

### Editing shortcuts

| Shortcut | Action |
|----------|--------|
| **Ctrl+Z / Ctrl+Y** | Undo / Redo |
| **Ctrl+S** | Save the world |
| **Ctrl+C / Ctrl+V** | Copy / paste the selected actor |
| **Delete** | Delete the selected actor |

Undo/redo restore the full scene (actors, names, meshes, lights, hierarchy); camera and render-mode are treated as view settings and stay out of undo.

### Outliner hierarchy (parent/child)

- **Drag one actor onto another** to parent it (the child keeps its world position; cycles are rejected).
- Tree nodes **collapse/expand**; **moving a parent moves its children**.
- Right-click → **Unparent**, or drag to empty space, to detach to the world root.

### Render modes

Pick the mode from the toolbar; it is saved into the world and used by the standalone game too.

| Mode | Description |
|------|-------------|
| **Rasterizer** | CPU software rasterizer with **Flat / Gouraud / Phong** shading (the graded path). Pick the shading model + optional Depth view from the toolbar. |
| **GPU RT** | GPU ray tracer: hard/soft shadows, hemisphere GI, HDRI/gradient sky, mirror reflection, diffuse textures. |
| **Hybrid** | CPU rasterized G-buffer + GPU ray-traced shadows/GI. |

### Play-in-Editor (PIE)

- **Play** runs the world in the same window (physics + actor ticks); **Stop** reverts.
- **Play (Window)** saves the world and launches it as a **separate game process** (`Test.exe --game`).

---

## Worlds & assets

### Content browser

The Content Browser lists assets under `Content/`, grouped by tab (World / Mesh / Material / Texture). Double-click a `.world` to open it, a mesh to add it to the scene (or drag it onto the viewport to place it), or a material to open the material editor. Right-click for New World / New Material / Import.

### Importing models

Import via the Content Browser, the **Details mesh slot**, or by **dragging a file onto the window**. Supported: **`.obj`** (hand-written parser, with `.mtl`), **`.fbx`** (Assimp), **`.mesh`** (engine binary). On import the file is **copied into `Content/`** (an OBJ brings its `.mtl` + textures), so reopening the project still resolves it.

### Materials

Each material is **Blinn-Phong**: diffuse (`kd`), specular (`ks`), shininess, **mirror** (`km`, shows in GPU RT), and an optional **diffuse texture** — all sampled in every render mode.

Materials can be **shared assets** (`.material`): right-click → New Material, then **double-click to open the material editor**. Editing a material updates **every object that uses it** (it's shared by path). Assign one by dragging it onto an object in the viewport, or via the Details **Material slot** (drop / pick / Edit / Clear). An imported OBJ's `.mtl` shows up as a material too. Saving writes a `.material` (a `.mtl` source is written as `<stem>.material`, never overwritten).

**Texture tiling (UV repeat)** lives on the *mesh component*, not the material — so the same material can repeat differently per object. Set it in Details → **Texture Tiling**.

### Lights

Add from the Outliner:

- **Point Light** — position follows the actor; color × intensity.
- **Environment Light** — drives **hemisphere GI** (Unreal-Lumen-style) + the sky. Details gives you:
  - **Time of Day** — one slider (0–24 h, + Sunrise/Noon/Sunset/Night presets) moves the sun across the day, setting the sky gradient + light color/intensity.
  - **Sky Image (HDRI)** — drop or pick an image; it's **copied into `Content/`** and **saved with the world**, so it's restored on reload. The HDRI replaces the gradient in every mode.
  - Manual **Sky Horizon / Zenith / Exponent** for fine-tuning.

### Physics

Add a **Sphere** or **Box** collider to an actor (Details → Add Component). Toggle **Simulate Physics** off to make it **Static** (collides but immovable — e.g. a floor). Colliders have a scalable size + offset and draw as a green wireframe when selected.

---

## Render settings

Toolbar → **Render Settings**. Two independent profiles — **Editor** (live viewport) and **Game** (PIE / standalone) — let the editing view and the shipped game differ. Saved to `Config/EditorSettings.ini` (+ `Config/GameSettings.ini` for the game).

| Setting | Meaning |
|---------|---------|
| **Anti-Aliasing** | Off / SSAA 2× (supersample then downscale) |
| **Ambient Strength** | Base ambient brightness (raster) |
| **GI Samples** | Hemisphere GI rays per pixel (0 = off; needs an Environment Light) |
| **GI Bounces** | Diffuse path-trace depth (0 = AO, 1 = sky, 2+ = color bleed; GPU RT) |
| **GI Strength** | Environment-light / GI brightness multiplier |
| **Reflection Strength** | Mirror-reflection multiplier (GPU RT) |
| **Shininess** | Specular exponent |
| **Shadow Samples / Softness** | Soft-shadow ray count and penumbra width |

---

## Making your own project

Use the engine from a separate project instead of editing in-repo:

1. Double-click **`GenerateProject.bat`** — pick a folder and a name. It scaffolds `<name>\` with a `.sln`, `main.cpp`, `EngineRoot.props`, runtime DLLs, and **Editor / Game** build configurations.
2. By default the project is **linked** to this engine repo (engine edits propagate). Run **`Package.bat`** inside the generated folder to **freeze** the engine into the project for submission (self-contained; the repo can be moved/deleted afterward).

A minimal entry point just boots the engine and runs a project descriptor; the **Editor** build opens the editor, and the **Game** build runs the world directly (same exe, two configurations).

Set which world the project boots into via **File → Project Settings… → Default World** (saved to `Setting/DefaultEngine.ini`); the editor opens it on startup and a packaged game runs it.

---

## Repository layout

```
Engine/        Engine source (Framework, World, Mesh, Rasterizer, Render,
               RayTracing, Acceleration, Serialization, Threading, Import,
               Editor, Light, Physics, Player)
Test/          main.cpp — runs the editor / --game / --demo / --hw6 / --fbxtest
Template/      Scaffold used by GenerateProject
include/ lib/ bin/   Vendored GLEW/GLFW/GLM/Assimp/ImGui + runtime DLLs
Engine.sln     Browse / build / run solution
```

Engine internals and contribution conventions are documented in **[CLAUDE.md](CLAUDE.md)**.
