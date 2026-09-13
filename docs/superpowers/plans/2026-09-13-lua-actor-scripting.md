# Lua Actor Scripting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Embed Lua 5.4 and complete reliable `BeginPlay`, `Tick`, and `EndPlay` execution for Lua files attached to actors through a serializable script component.

**Architecture:** One engine-owned `UScriptSubsystem` owns the Lua VM, binding registry, compiled-chunk cache, and per-component script instances. Actors own polymorphic `UActorComponent` objects; scene, mesh, physics, light, and script components participate in one lifecycle and serialization model. The first public Lua surface is deliberately limited to `Engine.Log` plus a C++ registration seam for future APIs.

**Tech Stack:** C++17, vendored Lua 5.4.9 C sources, existing subsystem/world/actor framework, JSON world serializer, Dear ImGui editor, standalone C++ tests and Visual Studio projects.

**Spec:** `docs/superpowers/specs/2026-09-13-hardware-raster-lua-scripting-design.md`

## Global Constraints

- First milestone supports Actor Script Components only. Do not implement Project Script, World Script, input, actor-transform, spawning, or reflection bindings.
- A Lua script cannot rotate a cube in this milestone because no Actor API is exposed. Use the existing C++ `AActor::Tick` path for the assignment's rotating cube until that API is designed.
- Execute scripts only during Play/PIE and standalone game sessions, never merely because an editor world is open.
- One broken script must not stop other actors, the world tick, or shutdown.
- Each component gets isolated Lua instance state even when multiple components share one compiled script asset.
- Stop PIE cleanly, call `EndPlay` once for successfully started instances, destroy all runtime state, and restore the editor world through the existing PIE rollback model.
- No hot reload in the first milestone. A changed script is picked up on the next Play session.
- Resolve serialized script paths relative to the project content root and reject traversal outside it.
- Add every new engine `.h`, `.cpp`, and vendored Lua library source to `Engine.vcxproj`, `Test/Test.vcxproj`, `Template/Template.vcxproj`, and corresponding `.filters` files. Standalone `Test/*_test.cpp` files keep their own `main()` and are not added to `Test.vcxproj`.
- Keep existing user changes and `docs/index.bleve/` untouched.

## Verification Command Convention

Every new standalone test file must start with its complete MSYS2 UCRT64 compile command, including every production `.cpp` it links. Run that command from the repository root with `C:\msys64\ucrt64\bin\g++.exe`, then run the emitted `.exe`; a red step expects compilation or assertions to fail and a green step expects exit code 0. Build all integrated targets with:

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild Engine.sln /m /p:Configuration=Debug /p:Platform=Win32 /clp:ErrorsOnly
& .\bin\Test.exe
```

---

## Task 1: Vendor and Build Lua 5.4.9

**Files:**

- Create: `ThirdParty/Lua/5.4.9/` from the official Lua source archive
- Create: `ThirdParty/Lua/README.md`
- Create: `Engine/Script/LuaInclude.h`
- Create: `Test/LuaSmokeTest.cpp`
- Modify: all six Visual Studio project and filter files

**Pinned dependency:**

- URL: `https://www.lua.org/ftp/lua-5.4.9.tar.gz`
- SHA-256: `2335b6c582a52654f94612bf10d2f4672805d05329aa6564e1bb8cd9e5c6fb8e6`
- Compile Lua as C in all three projects. Exclude the standalone `lua.c` and `luac.c` programs; include the library sources.

**Wrapper:**

```cpp
#pragma once
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
```

- [ ] Add `LuaSmokeTest.cpp` that creates a state, opens only the libraries intended by Task 3, evaluates `return 6 * 7`, checks integer 42, and closes the state.
- [ ] Compile it before vendoring; expect missing Lua headers/symbols.
- [ ] Download the pinned archive, verify the exact SHA-256 before extraction, copy its license/readme and source directory into `ThirdParty/Lua/5.4.9`, and document origin/checksum/local modifications.
- [ ] Add the library C files and include paths to every Visual Studio project; ensure `/TC` is used for Lua sources where required.
- [ ] Compile/run the smoke test and all three `Debug|Win32` projects; expect success with no duplicate `main`.
- [ ] Commit: `git add ThirdParty/Lua Engine/Script/LuaInclude.h Test/LuaSmokeTest.cpp **/*.vcxproj* && git commit -m "build: vendor Lua 5.4.9"`

## Task 2: Introduce `UActorComponent` and Centralize Actor Ownership

**Files:**

- Create: `Engine/World/UActorComponent.h`
- Create: `Engine/World/UActorComponent.cpp`
- Modify: `Engine/World/AActor.h`
- Modify: `Engine/World/AActor.cpp`
- Modify: `Engine/World/USceneComponent.h`
- Modify: `Engine/World/USceneComponent.cpp`
- Modify: `Engine/Physics/UPrimitiveComponent.*`
- Modify: `Engine/Mesh/UMeshComponent.h`
- Modify: `Engine/Mesh/UMeshComponent.cpp`
- Modify: `Engine/Light/ALight.h`
- Modify: `Engine/Light/ALight.cpp`
- Modify: `Engine/Light/LightComponent.h`
- Modify: `Engine/Light/LightComponent.cpp`
- Create: `Test/ActorComponentLifecycleTest.cpp`
- Modify: project/filter files

**Interfaces:**

```cpp
class UActorComponent {
public:
    virtual ~UActorComponent() = default;
    virtual std::string_view TypeName() const = 0;
    virtual void BeginPlay() {}
    virtual void Tick(float deltaSeconds) {}
    virtual void EndPlay() {}

    AActor* GetOwner() const noexcept;
    bool IsEnabled() const noexcept;
    void SetEnabled(bool enabled) noexcept;
};

class AActor {
public:
    template<class T, class... Args> T& AddComponent(Args&&... args);
    template<class T> T* FindComponent() const;
    const std::vector<std::unique_ptr<UActorComponent>>& Components() const;
};
```

`USceneComponent` derives from `UActorComponent`. Existing convenience members such as `mesh`, `physics`, and light accessors remain non-owning pointers into the actor-owned component array during migration.

- [ ] Write a fake component test covering construction ownership, owner assignment, insertion order, enabled filtering, `BeginPlay -> Tick -> EndPlay`, and exactly-once destruction.
- [ ] Run it; expect failure because there is no polymorphic actor component base.
- [ ] Implement actor-owned `std::unique_ptr<UActorComponent>` storage and lifecycle dispatch. Keep the root scene component's transform hierarchy semantics intact.
- [ ] Move primitive/physics, mesh, and light components under this ownership model and remove their manual deletes. Add assertions preventing one component from being owned by two actors.
- [ ] Run lifecycle tests, load/delete actors with each component type, and run a sanitizer/debug-heap smoke test for double delete or leaks.
- [ ] Commit: `git add Engine/World Engine/Mesh Engine/Physics Engine/Light Test/ActorComponentLifecycleTest.cpp Engine.vcxproj Engine.vcxproj.filters Test/Test.vcxproj Test/Test.vcxproj.filters Template/Template.vcxproj Template/Template.vcxproj.filters && git commit -m "refactor: unify actor component ownership"`

## Task 3: Create the Binding Registry and `UScriptSubsystem`

**Files:**

- Create: `Engine/Script/FLuaBindingRegistry.h`
- Create: `Engine/Script/FLuaBindingRegistry.cpp`
- Create: `Engine/Script/UScriptSubsystem.h`
- Create: `Engine/Script/UScriptSubsystem.cpp`
- Create: `Test/ScriptSubsystemTest.cpp`
- Modify: subsystem registration/bootstrap source
- Modify: project/filter files

**Interfaces:**

```cpp
using FLuaBindingInstaller = std::function<void(lua_State*)>;

class FLuaBindingRegistry {
public:
    void Register(std::string name, FLuaBindingInstaller installer);
    void InstallAll(lua_State* state) const;
};

class UScriptSubsystem final : public USubsystem {
public:
    void OnStartup() override;
    void OnShutdown() override;
    FLuaBindingRegistry& Bindings();
    lua_State* StateForTests() const;
};
```

**First Lua API:**

```lua
Engine.Log("message")
```

- [ ] Test subsystem startup creates one VM, duplicate binding names are rejected, `Engine.Log` reaches an injected log sink, and shutdown closes the VM exactly once.
- [ ] Run it; expect missing subsystem types.
- [ ] Implement the VM owner and binding registry. Open only base, table, string, math, and utf8 libraries; do not expose `io`, `os`, `package`, `debug`, `dofile`, or `loadfile`.
- [ ] Register `Engine.Log` through the same public registry future engine modules will use, and convert Lua type errors into contextual engine diagnostics.
- [ ] Register `UScriptSubsystem` in the existing subsystem manager and verify startup precedes world `BeginPlay`, while shutdown follows world `EndPlay`.
- [ ] Commit: `git add Engine/Script/FLuaBindingRegistry.* Engine/Script/UScriptSubsystem.* Test/ScriptSubsystemTest.cpp **/*.vcxproj* && git commit -m "feat: embed Lua script subsystem"`

## Task 4: Cache Script Assets and Isolate Runtime Instances

**Files:**

- Create: `Engine/Script/FLuaScriptAsset.h`
- Create: `Engine/Script/FLuaScriptCache.h`
- Create: `Engine/Script/FLuaScriptCache.cpp`
- Create: `Engine/Script/FLuaScriptInstance.h`
- Create: `Engine/Script/FLuaScriptInstance.cpp`
- Create: `Test/LuaScriptInstanceTest.cpp`
- Create: `Test/Fixtures/Scripts/StateIsolation.lua`
- Modify: `Engine/Script/UScriptSubsystem.h`
- Modify: `Engine/Script/UScriptSubsystem.cpp`
- Modify: project/filter files

**Interfaces:**

```cpp
struct FLuaScriptAsset {
    std::filesystem::path projectRelativePath;
    std::vector<std::byte> compiledChunk;
    std::uint64_t contentHash = 0;
};

class FLuaScriptInstance {
public:
    bool BeginPlay();
    bool Tick(float deltaSeconds);
    void EndPlay() noexcept;
    bool HasFunction(std::string_view name) const;
};
```

- [ ] Write a fixture whose local counter increments in `Tick`. Create two instances from one cached asset and assert both start at 0, then advance independently.
- [ ] Add rejection tests for absolute paths, `..` traversal, syntax errors, missing files, and non-function callback fields.
- [ ] Run the test; expect missing asset/instance types.
- [ ] Implement canonical path resolution constrained to the project content root, source read, `luaL_loadbufferx`, bytecode dump cache, and a separate environment table per instance with safe globals inherited through `__index`.
- [ ] Store callback references in the Lua registry, restore stack height after every call, and include file/callback/actor context in errors.
- [ ] Run the isolation and validation tests; confirm no instance can mutate another instance's locals.
- [ ] Commit: `git add Engine/Script/FLuaScriptAsset.h Engine/Script/FLuaScriptCache.* Engine/Script/FLuaScriptInstance.* Test/LuaScriptInstanceTest.cpp Test/Fixtures/Scripts/StateIsolation.lua **/*.vcxproj* && git commit -m "feat: cache Lua assets with isolated instances"`

## Task 5: Add and Serialize `UScriptComponent`

**Files:**

- Create: `Engine/Script/UScriptComponent.h`
- Create: `Engine/Script/UScriptComponent.cpp`
- Modify: `Engine/World/AActor.h`
- Modify: `Engine/World/AActor.cpp`
- Modify: `Engine/Serialization/FWorldSerializer.cpp`
- Modify: component factory definitions and registration
- Create: `Test/ScriptComponentSerializationTest.cpp`
- Create: `Test/Fixtures/ScriptComponentWorld.json`
- Modify: project/filter files

**Interfaces:**

```cpp
class UScriptComponent final : public UActorComponent {
public:
    std::string_view TypeName() const override { return "ScriptComponent"; }
    const std::filesystem::path& ScriptPath() const;
    void SetScriptPath(std::filesystem::path projectRelativePath);
};
```

**Serialized shape:**

```json
{
  "Type": "ScriptComponent",
  "Enabled": true,
  "Script": "Scripts/Rotator.lua"
}
```

- [ ] Test a world containing two script components saves and loads path/enabled state and preserves component order. Test unknown component types log and skip without losing the actor.
- [ ] Run it; expect failure because the existing factory and serializer accept only `USceneComponent` descendants.
- [ ] Generalize the component factory to construct `UActorComponent`, while keeping scene-parent links only for `USceneComponent` instances.
- [ ] Implement `UScriptComponent` data properties and format-2 serialization. Serialize project-relative paths with forward slashes and reject invalid paths before mutating component state.
- [ ] Re-run round-trip tests and load one pre-component-format world to verify backward compatibility.
- [ ] Commit: `git add Engine/Script/UScriptComponent.* Engine/World/AActor.* Engine/Serialization/FWorldSerializer.cpp Test/ScriptComponentSerializationTest.cpp Test/Fixtures/ScriptComponentWorld.json && git commit -m "feat: serialize actor script components"`

## Task 6: Wire Script Execution into Game and PIE Lifecycles

**Files:**

- Modify: `Engine/Script/UScriptComponent.*`
- Modify: `Engine/Script/UScriptSubsystem.*`
- Modify: `Engine/World/UWorld.*`
- Modify: editor PIE control source
- Modify: engine standalone loop source
- Create: `Test/Fixtures/Scripts/Lifecycle.lua`
- Create: `Test/ScriptLifecycleTest.cpp`

**Callback contract:**

```lua
function BeginPlay()
    Engine.Log("begin")
end

function Tick(deltaSeconds)
    Engine.Log("tick")
end

function EndPlay()
    Engine.Log("end")
end
```

- [ ] Write a lifecycle test that opens an editor world without playing and expects no messages, then performs Play, two ticks, Stop and expects exactly `begin, tick, tick, end`.
- [ ] Add a second Play/Stop cycle and assert fresh instance state, one `EndPlay`, and no callbacks after Stop.
- [ ] Run it; expect no script callbacks.
- [ ] On world `BeginPlay`, ask `UScriptSubsystem` to create instances for enabled components. Call optional callbacks via protected calls; missing callbacks are valid. Pass `deltaSeconds` as a Lua number.
- [ ] On Stop/world `EndPlay`, call `EndPlay` only for instances whose `BeginPlay` completed, then release every registry reference and cached session object. Do not let exceptions/Lua errors escape the component loop.
- [ ] Run lifecycle tests in editor-style and standalone-style harnesses. Verify actor deletion during play removes its instance safely.
- [ ] Commit: `git add Engine/Script Engine/World Test/ScriptLifecycleTest.cpp Test/Fixtures/Scripts/Lifecycle.lua && git commit -m "feat: run Lua actor lifecycle callbacks"`

## Task 7: Add Editor Assignment and Packaging Support

**Files:**

- Modify: actor/component inspector UI sources
- Modify: content browser/asset filtering sources
- Modify: project packaging/cooking source
- Create: `Content/Scripts/ExampleActor.lua`
- Modify: template project content manifest

- [ ] Add a packaging manifest test or dry-run command asserting every script referenced by a saved world is included at its project-relative path and an unreferenced script follows the project's documented content policy.
- [ ] Run the packaging check; expect referenced Lua files to be absent.
- [ ] Add Script Component creation/removal, enabled toggle, `.lua` asset picker, clear action, and validation message to the inspector. Never execute the selected script in edit mode.
- [ ] Teach content discovery and packaging to include `.lua` as data, without compiling it into the executable. Add a readable `ExampleActor.lua` using all three callbacks and `Engine.Log` only.
- [ ] Manually assign the example to two actors, save/reopen the world, run PIE, stop, package, and run standalone from a path containing spaces.
- [ ] Commit: `git add Engine/Editor Engine/Framework Template/Package.ps1 Template/Package.bat Content/Scripts/ExampleActor.lua && git commit -m "feat: assign and package Lua actor scripts"`

## Task 8: Prove Error Isolation, Cleanup, and Document the API Boundary

**Files:**

- Create: `Test/Fixtures/Scripts/BeginError.lua`
- Create: `Test/Fixtures/Scripts/TickError.lua`
- Create: `Test/Fixtures/Scripts/EndError.lua`
- Create: `Test/ScriptErrorIsolationTest.cpp`
- Modify: `README.md`
- Modify: `CLAUDE.md`
- Create: `docs/scripting/lua-actor-scripting.md`

- [ ] Test three actors where one fails in each lifecycle phase and healthy neighbors continue. Assert errors identify actor, component, path, callback, and Lua message; assert shutdown still releases all registry references.
- [ ] Run it before final hardening and record failures.
- [ ] Fix stack leaks, repeated-error spam, and lifecycle bookkeeping found by the test. Disable only the failing callback/instance according to the approved error policy; never stop the world.
- [ ] Document the one-VM architecture, component schema, allowed libraries, `Engine.Log`, callback signatures, Play-only execution, lack of hot reload, path rules, packaging, and the explicit absence of Actor APIs.
- [ ] State in the assignment-facing README that cube rotation currently uses C++ `AActor::Tick`; Lua attachment demonstrates scripting infrastructure but cannot manipulate transforms until a later API milestone.
- [ ] Run all standalone tests, build all three projects in `Debug|Win32`, execute two PIE cycles, and execute the packaged sample. Use debug heap or available leak tooling to verify the VM and instances are released.
- [ ] Commit: `git add Test/Fixtures/Scripts Test/ScriptErrorIsolationTest.cpp README.md CLAUDE.md docs/scripting/lua-actor-scripting.md && git commit -m "test: harden Lua actor scripting lifecycle"`

## Final Acceptance Checklist

- [ ] Lua 5.4.9 source and checksum are recorded and reproducible in every project target.
- [ ] Exactly one `UScriptSubsystem` VM exists per engine process and shuts down cleanly.
- [ ] Actors own polymorphic components without manual double-deletion paths.
- [ ] Script path and enabled state survive world save/load using project-relative paths.
- [ ] Merely opening an editor world runs no Lua; Play and standalone run the same callbacks.
- [ ] Callback order and exactly-once semantics pass for repeated Play/Stop sessions.
- [ ] Shared compiled assets still produce isolated per-component state.
- [ ] Missing files, syntax errors, runtime errors, and missing callbacks cannot stop healthy actors or engine shutdown.
- [ ] Packaged builds include referenced scripts and run from paths containing spaces.
- [ ] Documentation does not imply an Actor/transform binding exists in this milestone.
- [ ] Full Visual Studio `Debug|Win32` build and all standalone tests pass.
