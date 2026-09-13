// Standalone real-Lua test, deliberately outside Test.vcxproj.
// From the worktree root in MSYS2 UCRT64; compile Lua as C before C++ linking:
// gcc -std=c17 -I./ThirdParty/Lua/5.4.9/src -c ThirdParty/Lua/5.4.9/src/lapi.c ThirdParty/Lua/5.4.9/src/lauxlib.c ThirdParty/Lua/5.4.9/src/lbaselib.c ThirdParty/Lua/5.4.9/src/lcode.c ThirdParty/Lua/5.4.9/src/lcorolib.c ThirdParty/Lua/5.4.9/src/lctype.c ThirdParty/Lua/5.4.9/src/ldebug.c ThirdParty/Lua/5.4.9/src/ldo.c ThirdParty/Lua/5.4.9/src/ldump.c ThirdParty/Lua/5.4.9/src/lfunc.c ThirdParty/Lua/5.4.9/src/lgc.c ThirdParty/Lua/5.4.9/src/llex.c ThirdParty/Lua/5.4.9/src/lmathlib.c ThirdParty/Lua/5.4.9/src/lmem.c ThirdParty/Lua/5.4.9/src/lobject.c ThirdParty/Lua/5.4.9/src/lopcodes.c ThirdParty/Lua/5.4.9/src/lparser.c ThirdParty/Lua/5.4.9/src/lstate.c ThirdParty/Lua/5.4.9/src/lstring.c ThirdParty/Lua/5.4.9/src/lstrlib.c ThirdParty/Lua/5.4.9/src/ltable.c ThirdParty/Lua/5.4.9/src/ltablib.c ThirdParty/Lua/5.4.9/src/ltm.c ThirdParty/Lua/5.4.9/src/lundump.c ThirdParty/Lua/5.4.9/src/lutf8lib.c ThirdParty/Lua/5.4.9/src/lvm.c ThirdParty/Lua/5.4.9/src/lzio.c && g++ -std=c++17 -Wall -Wextra -I./Engine/Script -I./ThirdParty/Lua/5.4.9/src Test/LuaScriptInstanceTest.cpp Engine/Script/FLuaBindingRegistry.cpp Engine/Script/UScriptSubsystem.cpp Engine/Script/FLuaScriptCache.cpp Engine/Script/FLuaScriptInstance.cpp lapi.o lauxlib.o lbaselib.o lcode.o lcorolib.o lctype.o ldebug.o ldo.o ldump.o lfunc.o lgc.o llex.o lmathlib.o lmem.o lobject.o lopcodes.o lparser.o lstate.o lstring.o lstrlib.o ltable.o ltablib.o ltm.o lundump.o lutf8lib.o lvm.o lzio.o -o LuaScriptInstanceTest.exe && ./LuaScriptInstanceTest.exe

#include "UScriptSubsystem.h"
#include "FLuaScriptAsset.h"
#include "FLuaScriptCache.h"
#include "FLuaScriptInstance.h"
#include "LuaInclude.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

namespace fs = std::filesystem;

// Windows junctions need no symlink privilege. Exercise the actual filesystem
// containment boundary even with MinGW's unsupported create_directory_symlink.
static bool CreateDirectoryLink(const fs::path& target, const fs::path& link)
{
#ifdef _WIN32
    fs::create_directory(link);
    const HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const std::wstring substitute = L"\\??\\" + target.native();
    const std::wstring printable = target.native();
    struct Junction
    {
        DWORD tag;
        WORD length, reserved;
        WORD substituteOffset, substituteLength, printOffset, printLength;
        wchar_t buffer[4096];
    } data{};
    assert(substitute.size() + printable.size() + 2 < 4096);
    data.tag = IO_REPARSE_TAG_MOUNT_POINT;
    data.substituteLength = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    data.printOffset = data.substituteLength + sizeof(wchar_t);
    data.printLength = static_cast<WORD>(printable.size() * sizeof(wchar_t));
    data.length = 8 + data.printOffset + data.printLength + sizeof(wchar_t);
    std::copy(substitute.begin(), substitute.end(), data.buffer);
    std::copy(printable.begin(), printable.end(), data.buffer + substitute.size() + 1);
    DWORD returned = 0;
    const bool created = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &data,
        data.length + 8, nullptr, 0, &returned, nullptr) != 0;
    CloseHandle(handle);
    return created;
#else
    std::error_code error;
    fs::create_directory_symlink(target, link, error);
    return !error;
#endif
}

struct Project
{
    fs::path root = fs::temp_directory_path() /
        ("lua-instance-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project()
    {
        fs::create_directories(root / "Content/Scripts");
        fs::copy_file("Test/Fixtures/Scripts/StateIsolation.lua", root / "Content/Scripts/StateIsolation.lua");
    }
    ~Project() { std::error_code error; fs::remove_all(root, error); }
    void Write(const char* name, const char* code) const
    {
        std::ofstream file(root / "Content/Scripts" / name, std::ios::binary);
        file << code;
        assert(file.good());
    }
};

static void Run(lua_State* state, const char* code)
{
    const int top = lua_gettop(state);
    assert(luaL_dostring(state, code) == LUA_OK);
    lua_settop(state, top);
}

static void Context(const std::string& message, const std::string& path,
    const std::string& owner, const std::string& phase)
{
    assert(message.find(path) != std::string::npos);
    assert(message.find(owner) != std::string::npos);
    assert(message.find(phase) != std::string::npos);
}

static void Sentinel(lua_State* state)
{
    assert(lua_gettop(state) == 1 && lua_tointeger(state, -1) == 17);
}

// Catches recompilation of equivalent paths, empty bytecode, shared counter/global
// state, missing exact delta, or inherited mutable globals bypassing isolation.
static void CheckAssetsAndIsolation()
{
    Project project;
    std::vector<std::string> logs;
    UScriptSubsystem scripts([&](std::string_view message) { logs.emplace_back(message); });
    scripts.SetProjectRoot(project.root);
    scripts.Init();
    lua_State* state = scripts.State();
    if (!state) for (const auto& error : logs) std::cerr << error << '\n';
    assert(state);
    lua_pushinteger(state, 17);
    const auto asset = scripts.LoadScriptAsset("Content/Scripts/StateIsolation.lua");
    assert(asset && !asset->compiledChunk.empty() && asset->contentHash != 0);
    assert(asset->projectRelativePath == fs::path("Content/Scripts/StateIsolation.lua"));
    assert(scripts.LoadScriptAsset("Content/./Scripts//StateIsolation.lua") == asset);
    auto first = scripts.CreateScriptInstance(asset->projectRelativePath, "first");
    auto second = scripts.CreateScriptInstance(asset->projectRelativePath, "second");
    assert(first && second && first->Asset() == asset && second->Asset() == asset);
    assert(first->HasFunction("BeginPlay") && first->HasFunction("Tick") && first->HasFunction("EndPlay"));
    assert(!first->HasFunction("Unknown"));
    assert(first->BeginPlay() && first->Tick(0.25f) && first->Tick(0.5f));
    assert(second->BeginPlay() && second->Tick(0.25f));
    first->EndPlay();
    second->EndPlay();
    assert((logs == std::vector<std::string>{
        "begin:0", "tick:1:0.25", "tick:2:0.5", "begin:0", "tick:1:0.25", "end:2", "end:1"}));
    Run(state, "assert(instanceGlobal == nil and viaG == nil); assert(math.instanceValue == nil)");
    Sentinel(state);
    first.reset();
    second.reset();
    scripts.ClearScriptCache();
    scripts.Shutdown();
    assert(!scripts.State());
}

// Catches missing source caching, ignored Clear, mutation during live instances,
// and a shutdown that closes Lua before dropping instance references.
static void CheckCacheAndLifetime()
{
    Project project;
    project.Write("Change.lua", "Engine.Log('old')");
    std::vector<std::string> logs;
    UScriptSubsystem scripts([&](std::string_view message) { logs.emplace_back(message); });
    scripts.SetProjectRoot(project.root);
    scripts.Init();
    const auto old = scripts.LoadScriptAsset("Content/Scripts/Change.lua");
    project.Write("Change.lua", "Engine.Log('new')");
    assert(scripts.LoadScriptAsset("Content/Scripts/Change.lua") == old);
    auto instance = scripts.CreateScriptInstance("Content/Scripts/Change.lua", "owner");
    assert(instance && logs.back() == "old");
    bool clearRejected = false, rootRejected = false;
    try { scripts.ClearScriptCache(); } catch (const std::logic_error&) { clearRejected = true; }
    try { scripts.SetProjectRoot(project.root); } catch (const std::logic_error&) { rootRejected = true; }
    assert(clearRejected && rootRejected);
    instance.reset();
    scripts.ClearScriptCache();
    const auto changed = scripts.LoadScriptAsset("Content/Scripts/Change.lua");
    assert(changed && changed != old && changed->contentHash != old->contentHash);
    instance = scripts.CreateScriptInstance("Content/Scripts/Change.lua", "survivor");
    assert(instance && logs.back() == "new");
    scripts.Shutdown();
    assert(!scripts.State() && !instance->Tick(0.25f));
    instance->EndPlay();
    instance.reset();
    scripts.Init();
    assert(scripts.LoadScriptAsset("Content/Scripts/Change.lua") != changed);
}

// Catches traversal, absolute/drive-relative/ADS paths, prefix lookalikes,
// directories, missing files, and source compilation outside protected cleanup.
static void CheckPathRejections()
{
    Project project;
    std::vector<std::string> errors;
    UScriptSubsystem scripts([&](std::string_view message) { errors.emplace_back(message); });
    scripts.SetProjectRoot(project.root);
    scripts.Init();
    lua_pushinteger(scripts.State(), 17);
    project.Write("Syntax.lua", "function Tick(");
    const std::vector<fs::path> invalid = {
        "", project.root / "Content/Scripts/StateIsolation.lua", "../Content/Scripts/StateIsolation.lua",
        "Content/Scripts/../Scripts/StateIsolation.lua", "Scripts/StateIsolation.lua",
        "Content/ScriptsOther/StateIsolation.lua", "Content/Scripts", "Content/Scripts/Missing.lua",
        "C:Content/Scripts/StateIsolation.lua", "Content/Scripts/StateIsolation.lua:stream"
    };
    for (const auto& path : invalid)
    {
        const auto count = errors.size();
        assert(!scripts.CreateScriptInstance(path, "bad-path-owner"));
        assert(errors.size() == count + 1);
        Context(errors.back(), path.generic_string(), "bad-path-owner", "load");
        Sentinel(scripts.State());
    }
    assert(!scripts.CreateScriptInstance("Content/Scripts/Syntax.lua", "syntax-owner"));
    Context(errors.back(), "Content/Scripts/Syntax.lua", "syntax-owner", "compile");
    Sentinel(scripts.State());

    fs::create_directories(project.root / "Outside");
    { std::ofstream file(project.root / "Outside/Escape.lua"); file << "error('must not execute')"; }
    const auto link = project.root / "Content/Scripts/Link";
    assert(CreateDirectoryLink(project.root / "Outside", link));
    assert(!scripts.CreateScriptInstance("Content/Scripts/Link/Escape.lua", "escape-owner"));
    assert(errors.back().find("must not execute") == std::string::npos && "junction escaped canonical containment");
    Context(errors.back(), "Content/Scripts/Link/Escape.lua", "escape-owner", "load");
    Sentinel(scripts.State());
    // Remove just the known link; never recurse through a junction in cleanup.
    assert(fs::remove(link));
}

// Catches callback-field coercion, stack leaks on all execution failures,
// missing callback arguments/context, and native sink exceptions escaping Lua.
static void CheckFailuresAndOptionalCallbacks()
{
    Project project;
    project.Write("Empty.lua", "");
    project.Write("BadCallback.lua", "Tick = 42");
    project.Write("TopError.lua", "error('top failure')");
    project.Write("CallbackError.lua",
        "function BeginPlay() error('begin failure') end "
        "function Tick(dt) assert(select('#', dt) == 1); error('tick failure') end "
        "function EndPlay() error('end failure') end");
    project.Write("NativeError.lua", "function Tick(dt) Engine.Log('throw-native') end");
    std::vector<std::string> messages;
    UScriptSubsystem scripts([&](std::string_view message) {
        if (message == "throw-native") throw std::runtime_error("native failure");
        messages.emplace_back(message);
    });
    scripts.SetProjectRoot(project.root);
    scripts.Init();
    lua_pushinteger(scripts.State(), 17);
    auto empty = scripts.CreateScriptInstance("Content/Scripts/Empty.lua", "empty-owner");
    assert(empty && !empty->HasFunction("Tick") && empty->BeginPlay() && empty->Tick(0.25f));
    empty->EndPlay();
    assert(messages.empty());
    assert(!scripts.CreateScriptInstance("Content/Scripts/BadCallback.lua", "bad-owner"));
    Context(messages.back(), "Content/Scripts/BadCallback.lua", "bad-owner", "Tick");
    Sentinel(scripts.State());
    assert(!scripts.CreateScriptInstance("Content/Scripts/TopError.lua", "top-owner"));
    Context(messages.back(), "Content/Scripts/TopError.lua", "top-owner", "top-level");
    Sentinel(scripts.State());
    auto bad = scripts.CreateScriptInstance("Content/Scripts/CallbackError.lua", "callback-owner");
    assert(bad && !bad->BeginPlay());
    Context(messages.back(), "Content/Scripts/CallbackError.lua", "callback-owner", "BeginPlay");
    Sentinel(scripts.State());
    assert(!bad->Tick(0.25f));
    Context(messages.back(), "Content/Scripts/CallbackError.lua", "callback-owner", "Tick");
    Sentinel(scripts.State());
    bad->EndPlay();
    Context(messages.back(), "Content/Scripts/CallbackError.lua", "callback-owner", "EndPlay");
    Sentinel(scripts.State());
    auto native = scripts.CreateScriptInstance("Content/Scripts/NativeError.lua", "native-owner");
    assert(native && !native->Tick(0.25f));
    Context(messages.back(), "Content/Scripts/NativeError.lua", "native-owner", "Tick");
    assert(messages.back().find("native failure") != std::string::npos);
    Sentinel(scripts.State());
}

// Catches registry-reference leaks with a real environment finalizer: retained
// callback closures also retain this environment and prevent collection.
static void CheckReferenceRelease()
{
    Project project;
    project.Write("Collect.lua",
        "local env = _G; local savedLog = Engine.Log; "
        "local witness = setmetatable({}, {__gc = function() savedLog('collected') end}); "
        "function BeginPlay() assert(witness and env) end "
        "function Tick(dt) assert(witness and env) end "
        "function EndPlay() assert(witness and env) end");
    std::vector<std::string> logs;
    UScriptSubsystem scripts([&](std::string_view message) { logs.emplace_back(message); });
    scripts.SetProjectRoot(project.root);
    scripts.Init();
    auto instance = scripts.CreateScriptInstance("Content/Scripts/Collect.lua", "collect-owner");
    assert(instance);
    lua_gc(scripts.State(), LUA_GCCOLLECT);
    assert(logs.empty());
    instance.reset();
    lua_gc(scripts.State(), LUA_GCCOLLECT);
    assert((logs == std::vector<std::string>{"collected"}));
}

// Catches retaining the cache until after lua_close. A real close finalizer
// observes the last asset owner disappearing before the VM starts closing.
static void CheckShutdownDropsCacheBeforeClose()
{
    Project project;
    std::weak_ptr<const FLuaScriptAsset> asset;
    int closed = 0;
    UScriptSubsystem scripts([](std::string_view) { assert(false); });
    scripts.SetProjectRoot(project.root);
    scripts.Init();
    asset = scripts.LoadScriptAsset("Content/Scripts/StateIsolation.lua");
    assert(!asset.expired());
    struct Witness { std::weak_ptr<const FLuaScriptAsset>* asset; int* closed; };
    lua_State* state = scripts.State();
    auto* witness = static_cast<Witness*>(lua_newuserdatauv(state, sizeof(Witness), 0));
    witness->asset = &asset;
    witness->closed = &closed;
    lua_newtable(state);
    lua_pushcfunction(state, [](lua_State* vm) -> int {
        auto* observed = static_cast<Witness*>(lua_touserdata(vm, 1));
        assert(observed->asset->expired());
        ++*observed->closed;
        return 0;
    });
    lua_setfield(state, -2, "__gc");
    lua_setmetatable(state, -2);
    lua_setglobal(state, "closeWitness");
    scripts.Shutdown();
    assert(closed == 1 && asset.expired() && !scripts.State());
}

// Catches diagnostic conversion that allocates Lua strings after pcall has
// returned: numeric error objects plus allocation failure must not panic.
static void CheckNonStringErrorUnderMemoryPressure()
{
    Project project;
    project.Write("NumberError.lua", "function Tick(dt) FailLuaAllocation() end");
    project.Write("NumberTopError.lua", "FailLuaAllocation()");
    struct AllocationFault { lua_Alloc delegate = nullptr; void* data = nullptr; bool reject = false; } fault;
    std::vector<std::string> errors;
    UScriptSubsystem scripts([&](std::string_view message) { errors.emplace_back(message); });
    scripts.Bindings().Register("fault injection", [](lua_State* state) {
        lua_pushcfunction(state, [](lua_State* vm) -> int {
            void* data = nullptr;
            lua_getallocf(vm, &data);
            lua_pushnumber(vm, 12345.125);
            static_cast<AllocationFault*>(data)->reject = true;
            return lua_error(vm);
        });
        lua_setglobal(state, "FailLuaAllocation");
    });
    scripts.SetProjectRoot(project.root);
    scripts.Init();
    lua_State* state = scripts.State();
    fault.delegate = lua_getallocf(state, &fault.data);
    lua_setallocf(state, [](void* data, void* pointer, size_t oldSize, size_t newSize) -> void* {
        auto* allocation = static_cast<AllocationFault*>(data);
        if (allocation->reject && newSize) return nullptr;
        return allocation->delegate(allocation->data, pointer, oldSize, newSize);
    }, &fault);
    lua_atpanic(state, [](lua_State*) -> int {
        assert(false && "error conversion allocated outside pcall");
        return 0;
    });
    lua_pushinteger(state, 17);
    auto instance = scripts.CreateScriptInstance("Content/Scripts/NumberError.lua", "number-owner");
    assert(instance && !instance->Tick(0.25f));
    fault.reject = false;
    Context(errors.back(), "Content/Scripts/NumberError.lua", "number-owner", "Tick");
    Sentinel(state);
    assert(!scripts.CreateScriptInstance("Content/Scripts/NumberTopError.lua", "number-top-owner"));
    fault.reject = false;
    Context(errors.back(), "Content/Scripts/NumberTopError.lua", "number-top-owner", "top-level");
    Sentinel(state);
    Run(state, "assert(6 * 7 == 42)");
}

int main()
{
    CheckAssetsAndIsolation();
    CheckCacheAndLifetime();
    CheckPathRejections();
    CheckFailuresAndOptionalCallbacks();
    CheckReferenceRelease();
    CheckShutdownDropsCacheBeforeClose();
    CheckNonStringErrorUnderMemoryPressure();
    std::cout << "Lua script instances: PASS\n";
}
