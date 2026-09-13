# Lua Actor Scripting

## Scope and ownership

The engine embeds the official Lua 5.4.9 sources under `ThirdParty/Lua/5.4.9`. Those sources are upstream Lua, not project-authored code; provenance, SHA-256, license, and local build notes live in `ThirdParty/Lua/README.md`. The authored integration is the code under `Engine/Script` plus its Actor/World lifecycle, serializer, editor, generator, packaging, and tests.

There is exactly one engine-owned `lua_State` VM. `Engine` registers one `UScriptSubsystem` with `USubsystemManager`; `InitAll()` creates the VM before any gameplay `BeginPlay`, and `ShutdownAll()` closes it after every world and component has received `EndPlay`. A private Lua thread shares that VM and is used only as scratch stack for allocation-independent registry cleanup; it is not a second VM and is never exposed to scripts.

`FLuaScriptCache` reads a validated source file once per Play session, compiles it with Lua, and stores bytecode plus a content hash in a shared `FLuaScriptAsset`. Every enabled, assigned `UScriptComponent` creates its own `FLuaScriptInstance`. The instance loads the cached bytecode into a separate environment, clones mutable safe-library tables, and owns independent registry references for its environment and callbacks. Two attachments can therefore share a compiled asset without sharing globals, closures, counters, or mutable library state.

## Script contract

All callbacks are optional. Define only those the attachment needs:

```lua
function BeginPlay()
    Engine.Log("started")
end

function Tick(deltaSeconds)
    Engine.Log("tick")
end

function EndPlay()
    Engine.Log("stopped")
end
```

`BeginPlay()` runs once after the runtime world starts. `Tick(deltaSeconds)` runs once per playing world frame and receives a Lua number in seconds. `EndPlay()` runs once only for an instance whose `BeginPlay()` completed successfully. A script with none of these functions is valid.

The only engine binding is:

```lua
Engine.Log(message) -- message must be a string
```

Future C++ modules register APIs through `FLuaBindingRegistry`; they must not bypass the subsystem's registry/lifetime boundary.

The available standard libraries are exactly base, coroutine, table, string, math, and UTF-8. Scripts do not receive `io`, `os`, `package`, `debug`, `dofile`, `loadfile`, or per-instance `load`. There are no Actor, transform, World, Input, render, physics, spawning, or reflection APIs. Consequently Lua cannot rotate or move an Actor in this milestone. The assignment cube rotation remains the C++ implementation in `AActor::Tick`, and click-to-place is not part of this feature.

## Execution modes and lifecycle

Lua executes in Editor Play-in-Editor and standalone Game only. Loading, inspecting, assigning, saving, or ticking the editor's edit world never runs scripts. There is no hot reload: source changes made while playing apply after Stop, on the next Play when the session cache is cleared and assets are read again.

Editor PIE call stack:

```text
Engine::Engine
  -> register UScriptSubsystem
Engine::Run / Engine::BootWorld
  -> USubsystemManager::InitAll
    -> UScriptSubsystem::Init (create VM, safe globals, cache)
EditorEngine::OnPlay
  -> CopyWorld(editorWorld) (configuration-only ScriptComponent copies)
  -> pieWorld->SetScriptSubsystem
  -> UWorld::BeginPlay
    -> AActor::DispatchBeginPlay
      -> UScriptComponent::BeginPlay
        -> UScriptSubsystem::CreateScriptInstance
          -> FLuaScriptCache::Load
          -> FLuaScriptInstance::Initialize
        -> FLuaScriptInstance::BeginPlay
          -> Lua BeginPlay()
Editor frame -> UWorld::Tick -> AActor::DispatchTick
  -> UScriptComponent::Tick -> FLuaScriptInstance::Tick -> Lua Tick(dt)
```

Standalone Game call stack:

```text
Engine::Run
  -> GameEngine::OnStartup
  -> Engine::BootWorld(projectRoot)
    -> GameEngine::WorldSetting / FWorldSerializer::LoadFromFile
    -> UScriptSubsystem::SetProjectRoot
    -> USubsystemManager::InitAll
    -> UWorld::SetScriptSubsystem
    -> UWorld::BeginPlay -> AActor -> UScriptComponent -> FLuaScriptInstance
main loop
  -> Engine::Tick -> UWorld::Tick -> ... -> Lua Tick(dt)
```

Cleanup differs between an Editor PIE Stop and standalone process shutdown.

An Editor PIE Stop destroys only the runtime clone and leaves the subsystem VM alive for a later Play session:

```text
EditorEngine::OnStop
  -> pieWorld_->EndPlay
    -> AActor::DispatchEndPlay (component insertion order, then Actor)
      -> UScriptComponent::EndPlay
        -> FLuaScriptInstance::EndPlay -> protected Lua EndPlay()
        -> unref callback functions and instance environment
  -> delete pieWorld_ -> destroy its C++ Actors and ScriptComponents
  -> VM remains initialized for the next Editor Play
```

When the editor application exits, `Engine::Run` calls `EditorEngine::OnShutdown`, which performs that same `OnStop`, before subsystem shutdown. Standalone shutdown intentionally retains the C++ world object until after the VM has closed:

```text
Engine::Run exit (standalone Game)
  -> world_->EndPlay
    -> AActor::DispatchEndPlay (component insertion order, then Actor)
      -> UScriptComponent::EndPlay
        -> FLuaScriptInstance::EndPlay -> protected Lua EndPlay()
        -> unref callback functions and instance environment
  -> Engine::OnShutdown
  -> USubsystemManager::ShutdownAll
    -> UScriptSubsystem::Shutdown
      -> invalidate any outstanding instance handles and registry references
      -> clear compiled cache
      -> lua_close
  -> delete world_ -> destroy the C++ world, Actors, and ScriptComponents
```

All script callbacks and normal instance releases therefore finish before subsystem shutdown. Standalone's later C++ object destruction cannot call Lua: `UScriptComponent::EndPlay` already cleared its lifecycle state, and subsystem shutdown invalidated any outstanding handles before `lua_close`.

The editor world is never the PIE world. `CopyWorld` clones serialized configuration into a runtime world; Stop destroys that clone and reveals the untouched edit world. A second Play creates fresh Lua instance state.

## Paths, serialization, and packaging

Authored paths use forward slashes and are project-relative:

```text
Content/Scripts/ExampleActor.lua
```

The path must name a file strictly beneath `Content/Scripts`. Absolute/rooted/drive paths, `..`, control characters, directory-only paths, alternate-data-stream spellings, and paths that physically escape through a symlink, junction, or other reparse point are rejected. Source is opened and validated by its physical file identity before it is read. Backslashes accepted at assignment are normalized before storage and diagnostics.

World format 2 serializes stable configuration only:

```text
  [Component]
Type = ScriptComponent
Enabled = 1
Script = Content/Scripts/ExampleActor.lua
```

An unassigned Script Component is valid and omits the `Script` field. The VM, Lua stack, compiled bytecode, closures, environment, registry references, callback state, and mutable script variables are deliberately never serialized. Multiple Script Components remain in Actor component order through save/reload.

The Content Browser categorizes safe `.lua` files beneath `Content/Scripts` as Script assets. Assigning an external `.lua` copies it into that directory after validation. Generated projects receive the template `Content/Scripts` tree. Packaging treats scripts as data, preserves nested relative paths, and writes every safe in-root `.lua` file to `ScriptManifest.txt`; inclusion is not limited to scripts referenced by the current world.

Editor workflow:

1. Select an Actor in the World Outliner.
2. In Details, choose **+ Add Component → Script Component**. Repeat for multiple attachments.
3. Select `Script [index]`, then drop/choose an existing Script asset or use **Import External .lua...**.
4. Use **Enabled** for persistent configuration, **Clear** to keep an unassigned component, or **Remove Component** to remove the attachment.
5. Save, reopen, and confirm the component paths/order; no Lua runs in edit mode.
6. Press **Play**, observe callbacks, then **Stop**. Edits to source take effect on the next Play.
7. Generate/package normally. All valid files below `Content/Scripts` are included at their project-relative paths.

## Failure isolation and diagnostics

Every load and callback crosses a protected Lua boundary and restores the VM stack height:

- missing file, syntax error, invalid callback field, or top-level error: report the load failure and create no live instance;
- `BeginPlay` error: release only that instance; it receives neither Tick nor End;
- first `Tick` error: report once, release only that instance, and suppress later-frame spam; serialized `Enabled` remains unchanged;
- `EndPlay` error: report once, continue later component/Actor shutdown, and release all references;
- missing optional callback: success/no diagnostic.

Attachment diagnostics use this actionable shape:

```text
Lua Actor="ActorName" Component=ScriptComponent[3] Path="Content/Scripts/File.lua" Phase=Tick: Content/Scripts/File.lua:12: message
```

`Component` is the exact zero-based index in `AActor::Components()`, matching the editor's `Script [index]` label. Lua's message and source file/line are retained when Lua provides them; native missing-file failures include every available field even though no Lua line exists. Diagnostic sinks are guarded so logging failure cannot break world progress or cleanup.

## Code map

| Area | Main files |
|---|---|
| Lua include/build ownership | `Engine/Script/LuaInclude.h`, `ThirdParty/Lua/5.4.9`, all three `.vcxproj` files |
| VM and safe libraries | `Engine/Script/UScriptSubsystem.*` |
| binding extension seam | `Engine/Script/FLuaBindingRegistry.*` |
| path and physical containment | `Engine/Script/FScriptPath.*`, `FLuaScriptCache.*` |
| bytecode asset and isolated runtime | `FLuaScriptAsset.h`, `FLuaScriptInstance.*` |
| component/lifecycle dispatch | `UScriptComponent.*`, `Engine/World/AActor.*`, `UWorld.*` |
| world format | `Engine/Serialization/FWorldSerializer.*` |
| assignment/content UI | `Engine/Editor/FEditorScriptWorkflow.cpp`, `FEditorAssetWorkflow.*`, `EditorEngine.cpp` |
| generation/packaging | `Scripts/GenerateProject.ps1`, `Template/Package.ps1`, `Template/Template.vcxproj*` |
| example and tests | `Content/Scripts/ExampleActor.lua`, `Test/Fixtures/Scripts`, `Test/*Script*Test*` |

## Focused verification

Build the integrated Win32 solution first:

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild Engine.sln /m /p:Configuration=Debug /p:Platform=Win32 /clp:ErrorsOnly
```

The complete UCRT64 compile/link/run commands are kept at the top of `Test/LuaSmokeTest.cpp`, `ScriptSubsystemTest.cpp`, `LuaScriptInstanceTest.cpp`, and `ScriptErrorIsolationTest.cpp`. The Engine.lib-backed lifecycle, serialization, editor boot, and workflow tests likewise carry their exact MSVC commands in their file headers.

Verify the package-all-in-root manifest without freezing a project:

```powershell
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Template\Package.ps1 -ProjectDir <generated-project> -ScriptManifestOnly
```

Run the packaging acceptance harness (it generates a project in a path containing spaces and checks `Content/Scripts/ExampleActor.lua`):

```powershell
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Test\ScriptPackagingTest.ps1
```

## First-milestone limitations

- Actor Script Components only; no Project or World scripts.
- `Engine.Log` only; no gameplay object APIs, transforms, input, rendering, or physics bindings.
- no hot reload; restart Play to observe source edits.
- one VM per engine process, not one VM per Actor; state isolation is per-component environments/references.
- Windows/Visual Studio Win32 is the integrated target; UCRT64 tests are standalone GL-free acceptance binaries.
- no click-to-place behavior and no Lua-driven assignment cube rotation.
