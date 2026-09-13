# Hardware Rasterization and Lua Scripting Design

Date: 2026-09-13
Status: Approved design

## 1. Purpose

Replace the engine's real-time CPU software rasterizer with an OpenGL hardware rasterization pipeline, redefine ray tracing as optional effects layered on rasterized primary visibility, retain the old renderers as explicitly deprecated developer tools, and embed Lua 5.4 with actor-scoped script components.

This design preserves the engine's current material, lighting, editor, serialization, and standalone-game behavior while removing the CPU framebuffer bottleneck from the normal rendering path.

## 2. Goals

- Make OpenGL hardware rasterization the only normal primary-visibility path.
- Preserve Flat, Gouraud, and Phong shading behavior.
- Preserve materials, diffuse textures, UV tiling, multiple point lights, environment lighting, and HDRI skies.
- Express ray tracing as a master feature with independent shadow, global-illumination, and reflection controls.
- Require OpenGL 3.3 and optionally accelerate ray tracing with OpenGL 4.3 Compute Shaders and SSBOs.
- Retain the CPU software rasterizer and pure GPU ray tracer for education, regression tests, and visual comparison.
- Introduce a reusable feature-lifecycle system for Stable, Experimental, Deprecated, and Internal features.
- Embed Lua 5.4 without requiring a separate runtime installation.
- Support actor-attached Lua files with serialized configuration and Play-time lifecycle calls.
- Leave a clean binding registry so engine APIs can be designed and exposed later.

## 3. Non-goals

- Removing the legacy software rasterizer or pure GPU ray tracer.
- Using the legacy renderers as automatic production fallbacks.
- Implementing Project scripts, World scripts, or Lua-defined subsystems in the first Lua milestone.
- Exposing Actor, World, Input, rendering, or physics APIs to Lua in the first milestone.
- Running Lua while the editor is not playing.
- Persisting Lua VM state, stacks, closures, or runtime variables in world files.
- Supporting Lua hot reload in the first milestone.
- Building a keyframe, timeline, skeletal, or general-purpose animation system in this refactor. Assignment cube rotation may use a C++ `AActor::Tick` implementation until an Actor transform API is intentionally exposed to Lua.
- Implementing the assignment-specific click-to-place behavior in this refactor. The new renderer and scripting host will support that later feature, but it remains a separate change.

## 4. Top-level Architecture

`UWorldRenderer` becomes the single renderer used by both the editor viewport and standalone game. The caller supplies a world, render target, dimensions, and settings; the scene-gathering and pass selection logic is shared.

```text
Editor Viewport FBO ---+
                       +--> UWorldRenderer::Render(...)
Game Backbuffer -------+

UWorldRenderer
  -> Scene Gathering
  -> UHardwareRasterizer
  -> Hardware G-buffer
  -> URasterLightingPass
  -> optional URayTracedEffectsPass
  -> Composite / Sky / Post-process
  -> output target
```

Primary visibility always comes from hardware rasterization in the normal pipeline. Enabling ray tracing never replaces rasterization; it adds secondary visibility and lighting effects to the rasterized G-buffer.

## 5. Hardware Rasterization

### 5.1 GPU mesh cache

`UHardwareRasterizer` maintains one `FGPUMeshResource` per source `UMesh` and source revision.

```text
FGPUMeshResource
  - VAO
  - vertex buffer
  - index buffer
  - index count
  - source mesh identity
  - source revision
```

Multiple actors that reference the same `UMesh` share the GPU buffers. Actor transforms are supplied separately. Static mesh data is uploaded only when the resource is first used, the source mesh revision changes, the OpenGL context is recreated, or the renderer shuts down.

### 5.2 Geometry pass

The geometry pass binds the cached VAO and material resources, uploads per-draw transform and material parameters, and renders with `glDrawElements`. It writes a Multiple Render Target G-buffer containing the information required by both raster lighting and ray-traced effects:

- world position;
- world normal;
- albedo and texture result;
- specular, shininess, and mirror parameters;
- object/material identity;
- hardware depth.

The exact texture formats may be optimized during implementation, but both the OpenGL 3.3 and 4.3 paths must consume the same logical G-buffer contract.

### 5.3 Shading compatibility

Shader variants preserve the existing shading-model meanings:

- Flat uses one face normal and constant face lighting.
- Gouraud computes vertex lighting and interpolates the lit result.
- Phong interpolates surface attributes and computes lighting per fragment.

The normal renderer must retain current material overrides, shared material assets, diffuse textures, UV tiling, multiple point lights, environment light parameters, and HDRI sky behavior.

### 5.4 No CPU framebuffer bridge

The normal editor path renders directly to an FBO texture consumed by ImGui. It does not generate `scene.outputImage`, perform a CPU readback, or upload a CPU framebuffer with `glTexSubImage2D`. The standalone game renders through the same `UWorldRenderer` to its supplied target.

## 6. Ray-traced Effects

The user-facing renderer has a master Ray Tracing switch and three independent effect switches:

```text
Ray Tracing
  - Shadows
  - Global Illumination
  - Reflections
```

When the master switch is off, the renderer performs no ray-tracing BVH upload, traversal, or effect pass. Raster lighting produces the final lit scene.

When the master switch is on, `URayTracedEffectsPass` reads the hardware G-buffer, traces the enabled secondary rays, and produces an HDR result for the common composite/sky/post-process stages. The rasterizer remains responsible for primary visibility in every case.

### 6.1 Backend interface

Two implementations share one logical interface and output contract:

```text
IRayTracingBackend
  - GL33 Fragment/TBO backend
  - GL43 Compute/SSBO backend
```

The OpenGL 3.3 backend evolves the existing fragment-shader and texture-buffer implementation. The OpenGL 4.3 backend uses Compute Shaders and SSBOs for more direct writable storage and dispatch.

The common renderer and composite pass must not contain backend-specific resource knowledge.

### 6.2 Capability selection

The required baseline is OpenGL 3.3. At startup, `FGraphicsCapabilities` records the actual context version and required entry points.

```text
OpenGL below 3.3
  -> show a clear requirement error
  -> stop engine initialization

OpenGL 3.3+
  -> hardware rasterization available
  -> compatible Fragment/TBO RT available

OpenGL 4.3 + required Compute/SSBO capabilities
  -> compute RT available
```

Ray-tracing backend selection supports `Auto`, `Compatible`, and `Compute`. `Auto` prefers Compute when supported. If Compute initialization fails, `Auto` emits one warning and falls back to Compatible. An explicitly requested unsupported Compute backend reports the problem and disables ray tracing instead of silently changing the user's explicit selection.

If a ray-tracing pass fails after raster initialization, rasterization continues, ray tracing is disabled for the session, and the editor displays a warning. A hardware raster shader failure is fatal to normal rendering and must not silently select a deprecated renderer.

## 7. Render Settings and Serialization

The normal configuration is feature-based rather than a mutually exclusive render-mode enumeration.

```text
HardwareRaster = true
RayTracing = false
RayTracedShadows = true
RayTracedGI = true
RayTracedReflections = true
RayTracingBackend = Auto
```

`HardwareRaster` is recorded for format clarity but is mandatory in normal operation. Deprecated renderer selection is a developer override, not a world render feature.

World serialization gains an explicit format version. Old numeric `RenderMode` values are read only for migration:

```text
old Rasterizer (0)
  -> Hardware Raster
  -> Ray Tracing off

old GPU RT (1)
  -> Hardware Raster
  -> Ray Tracing on
  -> Shadows, GI, and Reflections on

old Hybrid (2)
  -> Hardware Raster
  -> Ray Tracing on
  -> retain compatible quality settings
```

New saves write named feature fields and no longer write the old numeric mode. A successful migration logs one informational message and does not count as an error.

## 8. Feature Lifecycle and Deprecation

Compile-time API deprecation and runtime feature metadata remain separate mechanisms.

### 8.1 Compile-time API deprecation

`ENGINE_DEPRECATED(message)` wraps standard C++ `[[deprecated(message)]]`. It is applied to legacy public entry points that new code should not call. Legacy implementation units and dedicated tests may use a narrow warning-suppression scope so normal builds are not flooded with intentional warnings.

### 8.2 Runtime feature registry

`FFeatureRegistry` stores `FFeatureDescriptor` values with:

- stable feature ID;
- display name;
- status: Stable, Experimental, Deprecated, or Internal;
- introduced and deprecated engine versions;
- replacement feature ID or guidance;
- removal policy: none or planned;
- human-readable purpose.

Initial registrations are:

| Feature | Status | Replacement | Removal |
| --- | --- | --- | --- |
| Hardware Rasterizer | Stable | None | None |
| Ray-traced Effects | Stable | None | None |
| OpenGL 4.3 Compute RT | Experimental | Compatible RT fallback | None |
| CPU Software Rasterizer | Deprecated | Hardware Rasterizer | None |
| Pure GPU Ray Tracer | Deprecated | Hardware Raster + Ray-traced Effects | None |

### 8.3 Developer UI

The normal Render Settings UI shows Hardware Rasterization and the Ray Tracing feature controls. It does not show the two legacy renderers.

Developer Settings contains `Show Deprecated Features`. When enabled, a Legacy Renderers section exposes explicit overrides for CPU Software Rasterizer and Pure GPU Ray Tracer. Each item shows an orange Deprecated badge, deprecation version, replacement, no-removal promise, and retained purpose.

Developer overrides are stored in local `Config/DeveloperSettings.ini`, not in a world or packaged game configuration. Activating one emits one warning per session. Tests may call the legacy paths directly without enabling a UI override.

## 9. Lua 5.4 Integration

### 9.1 Ownership

Lua 5.4 source and license are vendored and built with the engine so packaged projects do not require an external Lua installation.

`UScriptSubsystem`, owned by `USubsystemManager`, is the only layer that directly owns and operates the Lua VM.

```text
UScriptSubsystem
  - lua_State
  - FLuaBindingRegistry
  - compiled script asset cache
  - runtime script instance registry
  - error reporter
```

The subsystem initializes before Play lifecycle dispatch and shuts down after all script instances have ended.

### 9.2 First-milestone API surface

The first milestone exposes only a diagnostic `Engine.Log()` function. It supplies extension points for later work:

- register a Lua module;
- register a function;
- register a C++ type.

Actor, World, Input, rendering, and physics bindings are intentionally absent. Consequently, the first milestone proves attachment, loading, lifecycle, serialization, and isolation, but does not yet rotate a cube from Lua. Cube rotation becomes possible after the owner Actor transform API is designed and registered.

### 9.3 Safe standard library surface

The embedded runtime opens the base, coroutine, table, string, math, and UTF-8 libraries. File-system, process, native module, and debug access are not exposed by default. Script files must resolve beneath the project's content script directory. This keeps packaged behavior deterministic and prevents an attached gameplay script from gaining unrestricted host access accidentally.

## 10. Actor Script Components

### 10.1 Component foundation

Introduce `UActorComponent` as the non-spatial component base with owner, enabled state, lifecycle hooks, and serialization. `USceneComponent` derives from it and retains transform and hierarchy behavior. `UScriptComponent` derives directly from `UActorComponent` because a script attachment has no transform.

`AActor` owns its components and dispatches `BeginPlay`, `Tick`, and `EndPlay`. Existing mesh and physics convenience pointers remain non-owning aliases for compatibility while ownership is consolidated behind the actor.

### 10.2 Serialized state

`UScriptComponent` serializes only stable configuration:

```text
ComponentType = Script
Enabled = true
ScriptPath = Content/Scripts/RotateCube.lua
```

It does not serialize the Lua VM, execution stack, closures, registry references, or mutable script variables.

### 10.3 Assets and instances

A compiled `FLuaScriptAsset` is cached by normalized content path. Each attached component receives a separate `FLuaScriptInstance` containing:

- an asset reference;
- a validated owner Actor handle;
- an isolated `_ENV`;
- an independent state table;
- registry references for optional `BeginPlay`, `Tick`, and `EndPlay` functions.

Many actors may share one compiled asset without sharing mutable instance state.

### 10.4 Play behavior

Lua does not run in editor-edit mode.

```text
Play start
  -> clone/create PIE world
  -> create enabled script instances
  -> call BeginPlay once

Play frame
  -> call Tick(deltaTime) once per enabled instance

Play stop
  -> call EndPlay once
  -> release all instance registry references
  -> destroy PIE world
  -> restore untouched editor world
```

Standalone Game uses the same component lifecycle on its real game world. Script changes are picked up on the next Play; first-milestone hot reload is not supported.

### 10.5 Error isolation

All Lua lifecycle calls use protected calls with file and line diagnostics.

- A load or syntax error disables that component instance.
- A BeginPlay error disables that component instance.
- A Tick error is reported once and disables that instance to prevent per-frame log spam.
- An EndPlay error is reported but does not interrupt cleanup.
- Failure in one script does not stop other scripts, rendering, physics, or the editor.

## 11. Verification

### 11.1 GL-free tests

- render-feature combinations and pass selection;
- OpenGL 3.3/4.3 backend capability selection;
- migration from every legacy numeric render mode;
- feature metadata and Deprecated replacement information;
- `UScriptComponent` serialization round trip;
- Lua BeginPlay/Tick/EndPlay ordering;
- independent state for actors sharing one script asset;
- Lua failure isolation and log-spam prevention;
- complete instance release after Play stops.

### 11.2 OpenGL integration tests

- hardware raster shader compilation and linking;
- framebuffer and G-buffer attachment completeness;
- rendering one cube;
- rendering multiple independently transformed cubes;
- Flat, Gouraud, and Phong variants;
- material, texture, UV tiling, light, and HDRI behavior;
- Ray Tracing off and on pass selection;
- Compatible RT on OpenGL 3.3;
- Compute RT selection and Auto fallback on capable hardware;
- FBO recreation after viewport resize;
- absence of unexpected OpenGL errors.

Pixel-perfect output is not required across different GPU vendors. Image tests should use robust invariants or tolerances, while deterministic pass-selection and serialization behavior remains exact.

### 11.3 Performance invariants

Exercise scenes with 1, 100, and 1,000 cubes. Record Raster Only and Ray Tracing On separately. The required invariants are:

- zero per-frame geometry uploads for unchanged static meshes;
- zero BVH upload or ray-tracing dispatch when the master switch is off;
- zero normal-path CPU framebuffer generation/readback/upload;
- shared GPU geometry for actors using the same mesh;
- no unbounded GPU resource growth across world reloads or Play sessions.

## 12. Delivery Boundaries

Implementation should be staged so each architectural boundary can be verified independently:

1. feature lifecycle metadata and settings migration;
2. shared renderer entry point and GPU mesh cache;
3. hardware G-buffer and raster lighting parity;
4. GL 3.3 ray-traced effects integration;
5. optional GL 4.3 Compute/SSBO backend;
6. deprecated developer UI and legacy overrides;
7. Lua runtime and binding registry;
8. actor component foundation and script serialization;
9. Play-only Lua lifecycle and error isolation;
10. integration, performance, and regression verification.

The detailed implementation order, file edits, and test-first steps belong in the subsequent implementation plan.
