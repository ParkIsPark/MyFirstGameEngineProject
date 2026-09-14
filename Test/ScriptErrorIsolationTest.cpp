// Current complete recipe: powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name ScriptErrorIsolationTest
// Prerequisite: Engine.sln Debug|Win32. Older direct compile examples follow.
// Task 8 standalone acceptance test. Run these exact commands from the repository root in PowerShell:
// & 'C:\msys64\ucrt64\bin\gcc.exe' -std=c17 -DLUA_USE_APICHECK -I./ThirdParty/Lua/5.4.9/src -c ThirdParty/Lua/5.4.9/src/lapi.c ThirdParty/Lua/5.4.9/src/lauxlib.c ThirdParty/Lua/5.4.9/src/lbaselib.c ThirdParty/Lua/5.4.9/src/lcode.c ThirdParty/Lua/5.4.9/src/lcorolib.c ThirdParty/Lua/5.4.9/src/lctype.c ThirdParty/Lua/5.4.9/src/ldebug.c ThirdParty/Lua/5.4.9/src/ldo.c ThirdParty/Lua/5.4.9/src/ldump.c ThirdParty/Lua/5.4.9/src/lfunc.c ThirdParty/Lua/5.4.9/src/lgc.c ThirdParty/Lua/5.4.9/src/llex.c ThirdParty/Lua/5.4.9/src/lmathlib.c ThirdParty/Lua/5.4.9/src/lmem.c ThirdParty/Lua/5.4.9/src/lobject.c ThirdParty/Lua/5.4.9/src/lopcodes.c ThirdParty/Lua/5.4.9/src/lparser.c ThirdParty/Lua/5.4.9/src/lstate.c ThirdParty/Lua/5.4.9/src/lstring.c ThirdParty/Lua/5.4.9/src/lstrlib.c ThirdParty/Lua/5.4.9/src/ltable.c ThirdParty/Lua/5.4.9/src/ltablib.c ThirdParty/Lua/5.4.9/src/ltm.c ThirdParty/Lua/5.4.9/src/lundump.c ThirdParty/Lua/5.4.9/src/lutf8lib.c ThirdParty/Lua/5.4.9/src/lvm.c ThirdParty/Lua/5.4.9/src/lzio.c
// & 'C:\msys64\ucrt64\bin\g++.exe' -std=c++17 -Wall -Wextra -ffunction-sections -fdata-sections -I./include -I./Engine/Core -I./Engine/World -I./Engine/Physics -I./Engine/Mesh -I./Engine/Light -I./Engine/RayTracing -I./Engine/Serialization -I./Engine/Script -I./ThirdParty/Lua/5.4.9/src Test/ScriptErrorIsolationTest.cpp Engine/Core/UScene.cpp Engine/World/ACamera.cpp Engine/Serialization/FArchive.cpp Engine/World/UActorComponent.cpp Engine/World/USceneComponent.cpp Engine/World/AActor.cpp Engine/World/UWorld.cpp Engine/Physics/UPrimitiveComponent.cpp Engine/Physics/UPhysicsWorld.cpp Engine/Script/FScriptPath.cpp Engine/Script/FLuaBindingRegistry.cpp Engine/Script/FLuaScriptCache.cpp Engine/Script/FLuaScriptInstance.cpp Engine/Script/UScriptSubsystem.cpp Engine/Script/UScriptComponent.cpp lapi.o lauxlib.o lbaselib.o lcode.o lcorolib.o lctype.o ldebug.o ldo.o ldump.o lfunc.o lgc.o llex.o lmathlib.o lmem.o lobject.o lopcodes.o lparser.o lstate.o lstring.o lstrlib.o ltable.o ltablib.o ltm.o lundump.o lutf8lib.o lvm.o lzio.o '-Wl,--gc-sections' -o ScriptErrorIsolationTest.exe
// .\ScriptErrorIsolationTest.exe

#include "AActor.h"
#include "LuaInclude.h"
#include "UScriptComponent.h"
#include "UScriptSubsystem.h"
#include "UWorld.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace fs = std::filesystem;

namespace
{
    int failures = 0;

    void Check(bool condition, std::string_view label)
    {
        std::cout << (condition ? "PASS " : "FAIL ") << label << '\n';
        if (!condition) ++failures;
    }

    size_t Count(const std::vector<std::string>& values, std::string_view needle)
    {
        size_t count = 0;
        for (const auto& value : values)
            if (value.find(needle) != std::string::npos) ++count;
        return count;
    }

    const std::string* Find(const std::vector<std::string>& values, std::string_view needle)
    {
        for (const auto& value : values)
            if (value.find(needle) != std::string::npos) return &value;
        return nullptr;
    }

    size_t FindIndex(const std::vector<std::string>& values, std::string_view needle)
    {
        for (size_t index = 0; index != values.size(); ++index)
            if (values[index].find(needle) != std::string::npos) return index;
        return values.size();
    }

    size_t RegistryObjects(lua_State* state)
    {
        const int top = lua_gettop(state);
        size_t count = 0;
        lua_pushnil(state);
        while (lua_next(state, LUA_REGISTRYINDEX) != 0)
        {
            const int type = lua_type(state, -1);
            if (type == LUA_TTABLE || type == LUA_TFUNCTION || type == LUA_TTHREAD ||
                type == LUA_TUSERDATA) ++count;
            lua_pop(state, 1);
        }
        Check(lua_gettop(state) == top, "registry inspection restores VM stack");
        return count;
    }

    struct TempProject
    {
        fs::path root = fs::temp_directory_path() / ("Lua Task 8 " + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

        TempProject()
        {
            fs::create_directories(root / "Content/Scripts");
            for (const char* fixture : {"BeginError.lua", "TickError.lua", "EndError.lua"})
                fs::copy_file(fs::path("Test/Fixtures/Scripts") / fixture,
                    root / "Content/Scripts" / fixture);
            Write("HealthySame.lua",
                "function BeginPlay() Engine.Log('SAME_BEGIN') end\n"
                "function Tick(_) Engine.Log('SAME_TICK') end\n"
                "function EndPlay() Engine.Log('SAME_END') end\n");
            Write("HealthyNeighbor.lua",
                "function BeginPlay() Engine.Log('NEIGHBOR_BEGIN') end\n"
                "function Tick(_) Engine.Log('NEIGHBOR_TICK') end\n"
                "function EndPlay() Engine.Log('NEIGHBOR_END') end\n");
            Write("Optional.lua", "-- callbacks are optional\n");
            Write("SyntaxError.lua", "-- syntax line one\nfunction BeginPlay(\n");
        }

        ~TempProject()
        {
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }

        void Write(const char* name, const char* source)
        {
            std::ofstream output(root / "Content/Scripts" / name, std::ios::binary);
            output << source;
            Check(output.good(), std::string("write fixture ") + name);
        }
    };

    struct CountingActor final : AActor
    {
        int ticks = 0;
        void Tick(float) override { ++ticks; }
    };

    UScriptComponent& Attach(AActor& actor, const fs::path& path)
    {
        auto& component = actor.AddComponent<UScriptComponent>();
        component.SetScriptPath(path);
        return component;
    }

    bool HasContext(const std::string* diagnostic, std::string_view actor,
        std::string_view component, std::string_view path, std::string_view phase,
        std::string_view luaMessage, std::string_view line)
    {
        if (!diagnostic) return false;
        return diagnostic->find(std::string("Actor=\"") + std::string(actor) + "\"") != std::string::npos &&
            diagnostic->find(std::string("Component=") + std::string(component)) != std::string::npos &&
            diagnostic->find(std::string("Path=\"") + std::string(path) + "\"") != std::string::npos &&
            diagnostic->find(std::string("Phase=") + std::string(phase)) != std::string::npos &&
            diagnostic->find(luaMessage) != std::string::npos && diagnostic->find(line) != std::string::npos;
    }

    void RunCycle(UScriptSubsystem& scripts, std::vector<std::string>& output, int cycle)
    {
        UWorld world;
        auto same = std::make_unique<CountingActor>();
        same->name = "LifecycleActor";
        auto& beginFailure = Attach(*same, "Content\\Scripts\\BeginError.lua"); // component 0
        auto& tickFailure = Attach(*same, "Content/Scripts/TickError.lua"); // component 1
        auto& endFailure = Attach(*same, "Content/Scripts/EndError.lua"); // component 2
        auto& syntaxFailure = Attach(*same, "Content/Scripts/SyntaxError.lua"); // component 3
        auto& missingFailure = Attach(*same, "Content/Scripts/Missing.lua"); // component 4
        Attach(*same, "Content/Scripts/Optional.lua");                    // component 5
        Attach(*same, "Content/Scripts/HealthySame.lua");                 // component 6
        CountingActor* sameActor = same.get();
        world.Spawn(same.release());

        auto neighbor = std::make_unique<CountingActor>();
        neighbor->name = "HealthyNeighbor";
        Attach(*neighbor, "Content/Scripts/HealthyNeighbor.lua");
        CountingActor* neighborActor = neighbor.get();
        world.Spawn(neighbor.release());
        world.SetScriptSubsystem(&scripts);

        lua_State* state = scripts.State();
        const int initialTop = lua_gettop(state);
        const size_t baselineRegistryObjects = RegistryObjects(state);
        output.clear();

        world.BeginPlay();
        Check(lua_gettop(state) == initialTop, "BeginPlay restores VM stack");
        const size_t liveRegistryObjects = RegistryObjects(state);
        Check(liveRegistryObjects > baselineRegistryObjects, "live components own registry objects");
        world.Tick(0.25f);
        Check(lua_gettop(state) == initialTop, "first Tick restores VM stack");
        Check(RegistryObjects(state) == liveRegistryObjects,
            "Tick failure retains its begun instance references until Stop");
        world.Tick(0.5f);
        Check(lua_gettop(state) == initialTop, "later Tick restores VM stack");
        Check(Count(output, "TASK8_TICK_SENTINEL") == 1 &&
              Count(output, "TICK_ERROR_END") == 0,
            "failed Tick reports once, suppresses later Tick, and does not End before Stop");
        world.EndPlay();
        Check(lua_gettop(state) == initialTop, "EndPlay restores VM stack");
        Check(RegistryObjects(state) == baselineRegistryObjects,
            "Stop releases every component environment and callback reference");

        Check(sameActor->ticks == 2 && neighborActor->ticks == 2,
            "world and healthy neighboring Actors continue through failures");
        Check(beginFailure.IsEnabled() && tickFailure.IsEnabled() && endFailure.IsEnabled() &&
              syntaxFailure.IsEnabled() && missingFailure.IsEnabled(),
            "runtime failures do not alter serialized Enabled configuration");
        Check(Count(output, "TASK8_TICK_SENTINEL") == 1,
            "Tick failure reports exactly once across later frames");
        Check(Count(output, "FORBIDDEN_BEGIN_TICK") == 0 &&
              Count(output, "FORBIDDEN_BEGIN_END") == 0,
            "failed Begin instance never Ticks or Ends");
        Check(Count(output, "TICK_ERROR_END") == 1,
            "successfully begun Tick-failed instance receives exactly one End at Stop");
        Check(Count(output, "SAME_BEGIN") == 1 && Count(output, "SAME_TICK") == 2 &&
              Count(output, "SAME_END") == 1 && Count(output, "NEIGHBOR_BEGIN") == 1 &&
              Count(output, "NEIGHBOR_TICK") == 2 && Count(output, "NEIGHBOR_END") == 1,
            "later same-Actor attachment and neighboring Actor complete ordered lifecycle");
        Check(FindIndex(output, "SAME_BEGIN") < FindIndex(output, "NEIGHBOR_BEGIN") &&
              FindIndex(output, "TASK8_END_SENTINEL") < FindIndex(output, "SAME_END") &&
              FindIndex(output, "SAME_END") < FindIndex(output, "NEIGHBOR_END"),
            "healthy callbacks retain component and Actor dispatch order after failures");
        Check(Count(output, "TASK8_END_SENTINEL") == 1,
            "End failure reports once without interrupting later cleanup");
        Check(Count(output, "SyntaxError.lua") == 1 && Count(output, "Missing.lua") == 1,
            "syntax and missing-file load failures are isolated from healthy scripts");
        Check(Count(output, "Optional.lua") == 0,
            "optional missing callbacks remain non-errors");

        Check(HasContext(Find(output, "TASK8_BEGIN_SENTINEL"), "LifecycleActor",
            "ScriptComponent[0]", "Content/Scripts/BeginError.lua", "BeginPlay",
            "TASK8_BEGIN_SENTINEL", "BeginError.lua:3"),
            "Begin diagnostic has actor component normalized path phase message and line");
        Check(HasContext(Find(output, "TASK8_TICK_SENTINEL"), "LifecycleActor",
            "ScriptComponent[1]", "Content/Scripts/TickError.lua", "Tick",
            "TASK8_TICK_SENTINEL", "TickError.lua:7"),
            "Tick diagnostic has actor component normalized path phase message and line");
        Check(HasContext(Find(output, "TASK8_END_SENTINEL"), "LifecycleActor",
            "ScriptComponent[2]", "Content/Scripts/EndError.lua", "EndPlay",
            "TASK8_END_SENTINEL", "EndError.lua:11"),
            "End diagnostic has actor component normalized path phase message and line");
        Check(HasContext(Find(output, "SyntaxError.lua"), "LifecycleActor",
            "ScriptComponent[3]", "Content/Scripts/SyntaxError.lua", "load", "<eof>", ":3:"),
            "syntax diagnostic has full load context and source line");
        const std::string* missing = Find(output, "Missing.lua");
        Check(missing && missing->find("Actor=\"LifecycleActor\"") != std::string::npos &&
              missing->find("Component=ScriptComponent[4]") != std::string::npos &&
              missing->find("Path=\"Content/Scripts/Missing.lua\"") != std::string::npos &&
              missing->find("Phase=load") != std::string::npos,
            "missing-file diagnostic has all available actionable context");

        bool cacheReleased = true;
        try { scripts.ClearScriptCache(); }
        catch (...) { cacheReleased = false; }
        Check(cacheReleased, "Stop leaves no live subsystem instance registry");
        Check(cycle == 1 || Count(output, "SAME_BEGIN") == 1,
            "second Play starts with fresh script instances");
    }
}

int main()
{
#ifdef _MSC_VER
    _CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) |
        _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
    TempProject project;
    std::vector<std::string> output;
    UScriptSubsystem scripts([&](std::string_view value) { output.emplace_back(value); });
    scripts.SetProjectRoot(project.root);
    scripts.Init();
    Check(scripts.State() != nullptr, "real vendored Lua VM initializes");

    RunCycle(scripts, output, 1);
    RunCycle(scripts, output, 2);

    scripts.Shutdown();
    Check(scripts.State() == nullptr, "Shutdown closes the sole engine-owned VM");
    scripts.Shutdown();
    Check(scripts.State() == nullptr, "repeated Shutdown remains idempotent");
    return failures == 0 ? 0 : 1;
}
