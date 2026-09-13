#include "UScriptSubsystem.h"
#include "LuaInclude.h"

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace
{
    int OpenLibraries(lua_State* state)
    {
        const luaL_Reg libraries[] = {
            {LUA_GNAME, luaopen_base}, {LUA_COLIBNAME, luaopen_coroutine},
            {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string},
            {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8}
        };
        for (const auto& library : libraries)
        {
            luaL_requiref(state, library.name, library.func, 1);
            lua_pop(state, 1);
        }
        lua_pushnil(state);
        lua_setglobal(state, "dofile");
        lua_pushnil(state);
        lua_setglobal(state, "loadfile");
        return 0;
    }
}

UScriptSubsystem::UScriptSubsystem(FLuaLogSink logSink) : logSink_(std::move(logSink))
{
    if (!logSink_)
        logSink_ = [](std::string_view message) { std::cout << "[Lua] " << message << '\n'; };
    bindings_.Register("Engine.Log", [this](lua_State* state) {
        lua_newtable(state);
        lua_pushlightuserdata(state, this);
        lua_pushcclosure(state, Log, 1);
        lua_setfield(state, -2, "Log");
        lua_setglobal(state, "Engine");
    });
}

UScriptSubsystem::~UScriptSubsystem() { Shutdown(); }

void UScriptSubsystem::Init()
{
    if (state_) return;
    try
    {
        state_ = luaL_newstate();
        if (!state_) throw std::runtime_error("cannot allocate Lua state");
        lua_pushcfunction(state_, OpenLibraries);
        if (lua_pcall(state_, 0, 0, 0) != LUA_OK)
        {
            const char* error = lua_tostring(state_, -1);
            throw std::runtime_error(error ? error : "library initialization failed");
        }
        bindings_.InstallAll(state_);
    }
    catch (const std::exception& error)
    {
        Shutdown();
        ReportInitFailure(error.what());
    }
    catch (...)
    {
        Shutdown();
        ReportInitFailure("unknown native exception");
    }
}

void UScriptSubsystem::Shutdown()
{
    if (!state_) return;
    lua_State* closing = state_;
    state_ = nullptr;
    lua_close(closing);
}

FLuaBindingRegistry& UScriptSubsystem::Bindings() { return bindings_; }
lua_State* UScriptSubsystem::State() const noexcept { return state_; }

int UScriptSubsystem::Log(lua_State* state)
{
    if (lua_type(state, 1) != LUA_TSTRING)
        return luaL_error(state, "Engine.Log expects a string message");
    auto* scripts = static_cast<UScriptSubsystem*>(lua_touserdata(state, lua_upvalueindex(1)));
    size_t length = 0;
    const char* message = lua_tolstring(state, 1, &length);
    // No allocating Lua call may run inside a C++ catch scope: even pushing
    // an error string can longjmp on allocation failure. Trivial scratch storage
    // keeps both error conversion and lua_error outside exception unwinding.
    char sinkError[1024] = {};
    try
    {
        scripts->logSink_(std::string_view(message, length));
        return 0;
    }
    catch (const std::exception& error)
    {
        std::snprintf(sinkError, sizeof(sinkError), "Engine.Log sink failed: %s", error.what());
    }
    catch (...)
    {
        std::snprintf(sinkError, sizeof(sinkError), "Engine.Log sink failed: unknown native exception");
    }
    lua_pushstring(state, sinkError);
    return lua_error(state);
}

void UScriptSubsystem::ReportInitFailure(std::string_view detail) noexcept
{
    try { logSink_("UScriptSubsystem::Init: " + std::string(detail)); }
    catch (...)
    {
        // Diagnostic sinks must never turn a recoverable startup error into an
        // exception crossing the engine's lifecycle boundary.
        try { std::cerr << "[Lua] UScriptSubsystem::Init: " << detail << '\n'; }
        catch (...) {}
    }
}
