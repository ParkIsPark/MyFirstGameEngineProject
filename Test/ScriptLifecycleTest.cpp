// Real-Lua lifecycle integration, outside Test.vcxproj. From repository root:
// $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
// $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
// & $msbuild Engine.sln /m /p:Configuration=Debug /p:Platform=Win32 /clp:ErrorsOnly
// $cmd = 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 >nul && cl /nologo /std:c++17 /EHsc /MDd /DWIN32 /D_DEBUG /Iinclude /IEngine\Serialization /IEngine\World /IEngine\Script /IEngine\Core /IEngine\Light /IEngine\Mesh /IEngine\Physics /IEngine\Import /IEngine\RayTracing /IEngine\Acceleration Test\ScriptLifecycleTest.cpp /Fe:script_lifecycle_test.exe bin\Engine.lib /link /LIBPATH:lib glew32.lib freeglut.lib glfw3dll.lib opengl32.lib glu32.lib assimp-vc143-mt.lib'; & cmd.exe /d /c $cmd
// $env:Path = "$PWD\bin;$env:Path"; .\script_lifecycle_test.exe
// Engine.lib supplies real Task 2-6 world/component/serializer/subsystem/cache/
// instance implementations and the pinned Lua C sources, without a GL context.
#include "UWorld.h"
#include "AActor.h"
#include "UScriptComponent.h"
#include "UScriptSubsystem.h"
#include "FWorldSerializer.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include <sstream>
#include <functional>

struct NativeFailure : UActorComponent {
    std::string_view TypeName() const override { return "NativeFailure"; }
    int ticks = 0;
    void Tick(float) override { ++ticks; throw std::runtime_error("native component tick failure"); }
};
struct NativeActorFailure : AActor {
    int ticks = 0;
    void BeginPlay() override { throw std::runtime_error("native actor begin failure"); }
    void Tick(float) override { ++ticks; throw std::runtime_error("native actor tick failure"); }
};
struct NativeBeginFailure : UActorComponent {
    std::string_view TypeName() const override { return "NativeBeginFailure"; }
    void BeginPlay() override { throw std::runtime_error("native component begin failure"); }
};
struct NativeTickActor : AActor {
    int ticks = 0;
    void Tick(float) override { ++ticks; throw std::runtime_error("native actor tick failure"); }
};
struct OrderedEndActor : AActor {
    std::vector<std::string>* events = nullptr;
    void EndPlay() override { events->push_back("actor end"); }
};

namespace fs = std::filesystem;
int failures = 0;
void Check(bool value, const char* name) {
    std::cout << (value ? "PASS " : "FAIL ") << name << std::endl;
    if (!value) ++failures;
}
int main() {
    const auto root = fs::temp_directory_path() / ("Lua lifecycle " + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "Content/Scripts");
    fs::copy_file("Test/Fixtures/Scripts/Lifecycle.lua", root / "Content/Scripts/Lifecycle.lua");
    auto write = [&](const char* file, const char* source) { std::ofstream(root / "Content/Scripts" / file) << source; };
    write("Empty.lua", "-- no callbacks");
    write("BeginError.lua", "function BeginPlay() error('begin failure') end; function EndPlay() Engine.Log('bad end') end");
    write("TickError.lua", "function Tick() error('tick failure') end");
    std::vector<std::string> logs;
    std::function<void(std::string_view)> logCallback;
    UScriptSubsystem scripts([&](std::string_view s) { logs.emplace_back(s); if (logCallback) logCallback(s); });
    scripts.SetProjectRoot(root); scripts.Init();
    auto add = [](UWorld& w, const char* name, const char* path = "Lifecycle.lua") {
        auto actor = std::make_unique<AActor>(); actor->name = name;
        auto& c = actor->AddComponent<UScriptComponent>();
        if (path) c.SetScriptPath(fs::path("Content/Scripts") / path);
        w.Spawn(actor.get()); return actor.release();
    };
    {
        UWorld editor; add(editor, "Editor");
        const std::string saved = FWorldSerializer::Save(editor);
        auto play = std::unique_ptr<UWorld>(FWorldSerializer::Load(saved));
        play->Tick(.25f);
        Check(logs.empty(), "loading and editor ticking execute no Lua");
        play->SetScriptSubsystem(&scripts);
        play->BeginPlay(); play->BeginPlay();
        play->Tick(.25f); play->Tick(.5f); play->EndPlay(); play->EndPlay(); play->Tick(1);
        Check(logs == std::vector<std::string>{"begin:0", "tick:1:0.25", "tick:2:0.5", "end:2"}, "exact order delta and idempotent lifecycle");
        logs.clear();
        play->BeginPlay(); play->Tick(.125f); play->EndPlay(); play.reset();
        Check(logs == std::vector<std::string>{"begin:0", "tick:1:0.125", "end:1"}, "second Play has fresh state and destruction adds no End");
    }
    logs.clear();
    {
        UWorld world; auto* a = add(world, "First"); add(world, "Second");
        auto& extra = a->AddComponent<UScriptComponent>(); extra.SetScriptPath("Content/Scripts/Lifecycle.lua");
        auto* disabled = add(world, "Disabled"); disabled->FindComponent<UScriptComponent>()->SetEnabled(false);
        add(world, "Unassigned", nullptr); add(world, "Empty", "Empty.lua");
        world.SetScriptSubsystem(&scripts); world.BeginPlay(); world.Tick(.25f);
        Check(logs == std::vector<std::string>{"begin:0","begin:0","begin:0","tick:1:0.25","tick:1:0.25","tick:1:0.25"}, "actors and multiple attachments isolate state; disabled unassigned optional callbacks");
        extra.SetEnabled(false); world.EndPlay();
        Check(logs.size() == 9 && logs[6] == "end:1" && logs[7] == "end:1" && logs[8] == "end:1", "disabled-after-begin attachment still ends at Stop");
        scripts.ClearScriptCache();
    }
    logs.clear();
    {
        UWorld world; auto* a = add(world, "Destroyed");
        world.SetScriptSubsystem(&scripts); world.BeginPlay();
        Check(world.Destroy(a) && !world.Destroy(a) && world.GetScene().Actors.empty(), "Destroy owns exact actor and erases once");
        scripts.ClearScriptCache(); scripts.SetProjectRoot(root);
        world.Tick(.5f); world.EndPlay();
        Check(logs == std::vector<std::string>{"begin:0","end:0"}, "Destroy ends and releases before next tick");
        world.BeginPlay(); add(world, "Spawned"); world.Tick(.25f); world.EndPlay();
        Check(logs.size() == 5 && logs[2] == "begin:0" && logs[4] == "end:1", "Spawn between frames begins new actor");
    }
    logs.clear();
    {
        UWorld world; add(world, "Bad begin", "BeginError.lua"); add(world, "Bad tick", "TickError.lua"); add(world, "Healthy");
        world.SetScriptSubsystem(&scripts); world.BeginPlay(); world.Tick(.25f); world.Tick(.5f); world.EndPlay();
        Check(logs.size() == 6 && logs[0].find("begin failure") != std::string::npos && logs[1] == "begin:0" && logs[2].find("tick failure") != std::string::npos && logs[3] == "tick:1:0.25" && logs[5] == "end:2", "failed callbacks isolate and do not repeat or End failed Begin");
        scripts.ClearScriptCache();
    }
    logs.clear();
    {
        UWorld source; auto* actor = add(source, "Copy source"); source.SetScriptSubsystem(&scripts);
        source.BeginPlay(); source.Tick(.25f);
        UWorld copy; auto* cloned = new AActor();
        cloned->AddComponent<UScriptComponent>(*actor->FindComponent<UScriptComponent>());
        copy.Spawn(cloned);
        copy.Tick(1); source.Tick(.5f); source.EndPlay();
        copy.SetScriptSubsystem(&scripts); copy.BeginPlay(); copy.EndPlay();
        Check(logs == std::vector<std::string>{"begin:0","tick:1:0.25","tick:2:0.5","end:2","begin:0","end:0"}, "copying a live attachment copies configuration only");
    }
    logs.clear();
    {
        UWorld world;
        auto* actor = new NativeActorFailure(); actor->name = "Native actor";
        auto& native = actor->AddComponent<NativeFailure>();
        actor->AddComponent<UScriptComponent>().SetScriptPath("Content/Scripts/Lifecycle.lua"); world.Spawn(actor);
        add(world, "Neighbor"); world.SetScriptSubsystem(&scripts);
        std::ostringstream diagnostics; auto* previous = std::cerr.rdbuf(diagnostics.rdbuf());
        world.BeginPlay(); world.Tick(.25f); world.Tick(.5f); world.EndPlay();
        std::cerr.rdbuf(previous);
        Check(logs == std::vector<std::string>{"begin:0","begin:0","tick:1:0.25","tick:1:0.25","tick:2:0.5","tick:2:0.5","end:2","end:2"}, "native failures do not skip healthy later components or actors");
        Check(native.ticks == 1 && actor->ticks <= 1 && diagnostics.str().find("native actor begin failure") != std::string::npos && diagnostics.str().find("native component tick failure") != std::string::npos, "native failures observable and failing native ticks suppressed");
    }
    logs.clear();
    {
        UWorld world; auto* actor = new NativeTickActor();
        actor->AddComponent<NativeBeginFailure>();
        actor->AddComponent<UScriptComponent>().SetScriptPath("Content/Scripts/Lifecycle.lua");
        world.Spawn(actor); world.SetScriptSubsystem(&scripts);
        std::ostringstream diagnostics; auto* previous = std::cerr.rdbuf(diagnostics.rdbuf());
        world.BeginPlay(); world.Tick(.25f); world.Tick(.5f); world.EndPlay();
        std::cerr.rdbuf(previous);
        Check(actor->ticks == 1 && logs == std::vector<std::string>{"begin:0","tick:1:0.25","tick:2:0.5","end:2"} && diagnostics.str().find("native component begin failure") != std::string::npos && diagnostics.str().find("native actor tick failure") != std::string::npos, "native component Begin and actor Tick failures retain healthy script lifecycle");
    }
    logs.clear();
    {
        UWorld world; auto* actor = new OrderedEndActor(); actor->events = &logs;
        auto& script = actor->AddComponent<UScriptComponent>(); script.SetScriptPath("Content/Scripts/Lifecycle.lua");
        world.Spawn(actor); world.SetScriptSubsystem(&scripts); world.BeginPlay(); script.SetEnabled(false);
        bool rejected = false;
        logCallback = [&](std::string_view message) {
            if (message == "end:0") {
                try { actor->AddComponent<NativeFailure>(); } catch (const std::logic_error&) { rejected = true; }
            }
        };
        world.EndPlay(); logCallback = {};
        Check(logs == std::vector<std::string>{"begin:0","end:0","actor end"} && rejected, "disabled begun scripts End before Actor under mutation guard");
    }
    logs.clear();
    {
        UWorld world; auto* actor = add(world, "Reentrant"); auto* script = actor->FindComponent<UScriptComponent>();
        world.SetScriptSubsystem(&scripts); world.BeginPlay();
        bool retained = false;
        logCallback = [&](std::string_view message) {
            if (message == "end:0") {
                script->EndPlay();
                try { scripts.ClearScriptCache(); } catch (const std::logic_error&) { retained = true; }
            }
        };
        world.EndPlay(); logCallback = {};
        Check(retained && logs == std::vector<std::string>{"begin:0","end:0"}, "reentrant End retains live call until outer End returns");
        scripts.ClearScriptCache(); logs.clear(); world.BeginPlay(); retained = false;
        logCallback = [&](std::string_view message) {
            if (message == "tick:1:0.25") {
                script->EndPlay(); script->Tick(.5f);
                try { scripts.ClearScriptCache(); } catch (const std::logic_error&) { retained = true; }
            }
        };
        world.Tick(.25f); logCallback = {}; world.Tick(.5f); world.EndPlay(); scripts.ClearScriptCache();
        Check(retained && logs == std::vector<std::string>{"begin:0","tick:1:0.25","end:1"}, "stop requested inside Tick defers End and release until callback returns");
    }
    logs.clear();
    {
        UWorld world; add(world, "Refresh"); world.SetScriptSubsystem(&scripts);
        world.BeginPlay(); write("Lifecycle.lua", "function BeginPlay() Engine.Log('edited') end");
        world.Tick(.25f); world.EndPlay(); world.BeginPlay(); world.EndPlay();
        Check(logs == std::vector<std::string>{"begin:0","tick:1:0.25","end:1","edited"}, "file edits apply on next session only");
    }
    scripts.Shutdown();
    fs::remove_all(root);
    return failures ? 1 : 0;
}
