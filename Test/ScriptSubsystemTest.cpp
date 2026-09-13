// Standalone test; intentionally not part of Test.vcxproj.
// MSYS2 UCRT64, from the repository root (compile Lua as C, then link C++):
// gcc -std=c17 -I./ThirdParty/Lua/5.4.9/src -c ThirdParty/Lua/5.4.9/src/lapi.c ThirdParty/Lua/5.4.9/src/lauxlib.c ThirdParty/Lua/5.4.9/src/lbaselib.c ThirdParty/Lua/5.4.9/src/lcode.c ThirdParty/Lua/5.4.9/src/lcorolib.c ThirdParty/Lua/5.4.9/src/lctype.c ThirdParty/Lua/5.4.9/src/ldebug.c ThirdParty/Lua/5.4.9/src/ldo.c ThirdParty/Lua/5.4.9/src/ldump.c ThirdParty/Lua/5.4.9/src/lfunc.c ThirdParty/Lua/5.4.9/src/lgc.c ThirdParty/Lua/5.4.9/src/llex.c ThirdParty/Lua/5.4.9/src/lmathlib.c ThirdParty/Lua/5.4.9/src/lmem.c ThirdParty/Lua/5.4.9/src/lobject.c ThirdParty/Lua/5.4.9/src/lopcodes.c ThirdParty/Lua/5.4.9/src/lparser.c ThirdParty/Lua/5.4.9/src/lstate.c ThirdParty/Lua/5.4.9/src/lstring.c ThirdParty/Lua/5.4.9/src/lstrlib.c ThirdParty/Lua/5.4.9/src/ltable.c ThirdParty/Lua/5.4.9/src/ltablib.c ThirdParty/Lua/5.4.9/src/ltm.c ThirdParty/Lua/5.4.9/src/lundump.c ThirdParty/Lua/5.4.9/src/lutf8lib.c ThirdParty/Lua/5.4.9/src/lvm.c ThirdParty/Lua/5.4.9/src/lzio.c && g++ -std=c++17 -Wall -Wextra -I./Engine/Script -I./ThirdParty/Lua/5.4.9/src Test/ScriptSubsystemTest.cpp Engine/Script/FLuaBindingRegistry.cpp Engine/Script/UScriptSubsystem.cpp lapi.o lauxlib.o lbaselib.o lcode.o lcorolib.o lctype.o ldebug.o ldo.o ldump.o lfunc.o lgc.o llex.o lmathlib.o lmem.o lobject.o lopcodes.o lparser.o lstate.o lstring.o lstrlib.o ltable.o ltablib.o ltm.o lundump.o lutf8lib.o lvm.o lzio.o -o ScriptSubsystemTest.exe && ./ScriptSubsystemTest.exe

#include "FLuaBindingRegistry.h"
#include "UScriptSubsystem.h"
#include "LuaInclude.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

// Test-only allocation fault injection. Lua continues to use its real C
// allocator; only C++ allocation during the selected catch scope is rejected.
static bool rejectCppAllocation = false;

void* operator new(std::size_t size)
{
    if (rejectCppAllocation) throw std::bad_alloc();
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

struct InstallerAllocationFailure final : std::bad_alloc
{
    const char* what() const noexcept override
    {
        rejectCppAllocation = true;
        // Longer than small-string storage: allocating conversion must fail.
        return "allocation failure during installer diagnostics";
    }
    ~InstallerAllocationFailure() override { rejectCppAllocation = false; }
};

static void Run(lua_State* state, const char* code)
{
    const int status = luaL_dostring(state, code);
    if (status != LUA_OK) std::cerr << lua_tostring(state, -1) << '\n';
    assert(status == LUA_OK);
}

// Catches VM replacement on repeated Init, unsafe library exposure, lost stack
// balance, and failure to close/null the VM (a real Lua finalizer observes close).
static void CheckLifetimeAndLibraries()
{
    int closed = 0;
    UScriptSubsystem scripts([](std::string_view) { assert(false); });
    assert(scripts.State() == nullptr);
    scripts.Init();
    lua_State* state = scripts.State();
    assert(state != nullptr && lua_gettop(state) == 0);
    Run(state, "assert(_VERSION == 'Lua 5.4'); assert(6 * 7 == 42); "
        "for _, n in ipairs({'_G','coroutine','table','string','math','utf8'}) do "
        "assert(type(_G[n]) == 'table', n) end; "
        "for _, n in ipairs({'io','os','package','debug','dofile','loadfile'}) do "
        "assert(_G[n] == nil, n) end");
    auto** counter = static_cast<int**>(lua_newuserdatauv(state, sizeof(int*), 0));
    *counter = &closed;
    lua_newtable(state);
    lua_pushcfunction(state, [](lua_State* vm) -> int {
        ++**static_cast<int**>(lua_touserdata(vm, 1));
        return 0;
    });
    lua_setfield(state, -2, "__gc");
    lua_setmetatable(state, -2);
    lua_setglobal(state, "closeWitness");
    scripts.Init();
    assert(scripts.State() == state && lua_gettop(state) == 0);
    scripts.Shutdown();
    assert(scripts.State() == nullptr && closed == 1);
    scripts.Shutdown();
    assert(scripts.State() == nullptr && closed == 1);
}

// Catches accepted invalid/duplicate registration, changed installer order,
// repeated installation on Init, or an installer that leaks stack values.
static void CheckRegistryInstallation()
{
    UScriptSubsystem scripts;
    auto& bindings = scripts.Bindings();
    bindings.Register("first", [](lua_State* state) {
        Run(state, "installOrder = 'first'; installCount = (installCount or 0) + 1");
        lua_pushinteger(state, 99); // registry must discard installer scratch values
    });
    bindings.Register("second", [](lua_State* state) {
        Run(state, "installOrder = installOrder .. ',second'; installCount = installCount + 1; "
            "function customAnswer() return 42 end");
    });
    for (int kind = 0; kind != 3; ++kind)
    {
        bool rejected = false;
        try {
            if (kind == 0) bindings.Register("", [](lua_State*) { assert(false); });
            if (kind == 1) bindings.Register("empty", {});
            if (kind == 2) bindings.Register("first", [](lua_State*) { assert(false); });
        } catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected && scripts.State() == nullptr);
    }
    bool rejectedNull = false;
    try { bindings.InstallAll(nullptr); }
    catch (const std::invalid_argument&) { rejectedNull = true; }
    assert(rejectedNull);
    scripts.Init();
    scripts.Init();
    assert(scripts.State() && lua_gettop(scripts.State()) == 0);
    Run(scripts.State(), "assert(installOrder == 'first,second'); assert(installCount == 2); "
        "assert(customAnswer() == 42)");
}

// Catches missing bindings, duplicate sink calls, number-to-string coercion,
// embedded-NUL truncation, and missing contextual protected argument errors.
static void CheckLogging()
{
    std::vector<std::string> messages;
    UScriptSubsystem scripts([&](std::string_view message) { messages.emplace_back(message); });
    scripts.Init();
    Run(scripts.State(), "assert(type(Engine) == 'table' and type(Engine.Log) == 'function'); "
        "Engine.Log('forty-two')");
    assert((messages == std::vector<std::string>{"forty-two"}));
    Run(scripts.State(), "Engine.Log('a' .. string.char(0) .. 'b')");
    assert(messages.size() == 2 && messages[1] == std::string("a\0b", 3));
    Run(scripts.State(), "for _, value in ipairs({42, false, {}}) do "
        "local ok, err = pcall(Engine.Log, value); assert(not ok); "
        "assert(string.find(err, 'Engine.Log', 1, true)); "
        "assert(string.find(err, 'string', 1, true)) end; "
        "local ok, err = pcall(Engine.Log); assert(not ok and string.find(err, 'Engine.Log', 1, true))");
    assert(messages.size() == 2 && lua_gettop(scripts.State()) == 0);
}

// Catches Lua panics/escaped native exceptions during installation, missing
// binding-name context, leaked error stack, and partially initialized VM exposure.
static void CheckInstallationFailures()
{
    for (int failureKind : {0, 1, 2})
    {
        const char* expected[] = {"Lua failure", "native failure", "unknown native exception"};
        FLuaBindingRegistry registry;
        registry.Register("broken-binding", [failureKind](lua_State* state) {
            if (failureKind == 1) throw std::runtime_error("native failure");
            if (failureKind == 2) throw 42;
            luaL_error(state, "Lua failure");
        });
        lua_State* state = luaL_newstate();
        assert(state);
        lua_pushinteger(state, 17);
        bool contextual = false;
        try { registry.InstallAll(state); }
        catch (const std::runtime_error& error) {
            const std::string message = error.what();
            contextual = message.find("broken-binding") != std::string::npos &&
                message.find(expected[failureKind]) != std::string::npos;
        }
        assert(contextual && lua_gettop(state) == 1 && lua_tointeger(state, -1) == 17);
        lua_close(state);

        std::vector<std::string> messages;
        UScriptSubsystem scripts([&](std::string_view message) { messages.emplace_back(message); });
        scripts.Bindings().Register("broken-binding", [failureKind](lua_State* vm) {
            if (failureKind == 1) throw std::runtime_error("native failure");
            if (failureKind == 2) throw 42;
            luaL_error(vm, "Lua failure");
        });
        scripts.Init();
        assert(scripts.State() == nullptr && messages.size() == 1);
        assert(messages[0].find("Init") != std::string::npos);
        assert(messages[0].find("broken-binding") != std::string::npos);
        scripts.Shutdown();
    }
}

// Catches a second allocation exception escaping from an installer catch
// handler through C-compiled Lua, bypassing pcall's stack/frame restoration.
static void CheckAllocationFailureConversion()
{
    FLuaBindingRegistry registry;
    registry.Register("allocation-binding", [](lua_State*) { throw InstallerAllocationFailure{}; });
    lua_State* state = luaL_newstate();
    assert(state);
    lua_pushinteger(state, 17);
    bool contextual = false;
    bool escapedAllocation = false;
    try { registry.InstallAll(state); }
    catch (const std::runtime_error& error)
    {
        const std::string message = error.what();
        contextual = message.find("allocation-binding") != std::string::npos &&
            message.find("allocation failure during installer diagnostics") != std::string::npos;
    }
    catch (const std::bad_alloc&) { escapedAllocation = true; }
    assert(!escapedAllocation && "allocation exception escaped the Lua protected installer");
    assert(contextual && !rejectCppAllocation);
    assert(lua_gettop(state) == 1 && lua_tointeger(state, -1) == 17);
    Run(state, "return 6 * 7");
    assert(lua_gettop(state) == 2 && lua_tointeger(state, -1) == 42);
    lua_pop(state, 1);
    assert(lua_gettop(state) == 1 && lua_tointeger(state, -1) == 17);
    lua_close(state);
}

// Catches a C++ sink exception escaping through Lua instead of a protected error.
static void CheckSinkFailure()
{
    UScriptSubsystem scripts([](std::string_view) { throw std::runtime_error("sink failure"); });
    scripts.Init();
    Run(scripts.State(), "local ok, err = pcall(Engine.Log, 'hello'); assert(not ok); "
        "assert(string.find(err, 'Engine.Log', 1, true)); assert(string.find(err, 'sink failure', 1, true))");
}

int main()
{
    CheckLifetimeAndLibraries();
    CheckRegistryInstallation();
    CheckLogging();
    CheckInstallationFailures();
    CheckAllocationFailureConversion();
    CheckSinkFailure();
    std::cout << "script subsystem: PASS\n";
}
