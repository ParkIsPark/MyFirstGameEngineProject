#include "UScriptSubsystem.h"
#include "LuaInclude.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace
{
    int CreateRegistryThread(lua_State* state)
    {
        lua_State* registry = lua_newthread(state);
        if (!lua_checkstack(registry, 2)) return luaL_error(state, "cannot reserve Lua registry cleanup stack");
        const int reference = luaL_ref(state, LUA_REGISTRYINDEX);
        lua_pushinteger(state, reference);
        lua_pushlightuserdata(state, registry);
        return 2;
    }

    int InstanceGetMetatable(lua_State* state)
    {
        luaL_checkany(state, 1);
        // Primitive metatables belong to the shared VM. In particular the
        // string metatable exposes its shared string library through __index.
        if (!lua_istable(state, 1) && !lua_isuserdata(state, 1))
        {
            lua_pushnil(state);
            return 1;
        }
        if (!lua_getmetatable(state, 1)) { lua_pushnil(state); return 1; }
        lua_pushliteral(state, "__metatable");
        lua_rawget(state, -2);
        if (lua_isnil(state, -1)) lua_pop(state, 1);
        return 1;
    }

    int BuildSafeGlobals(lua_State* state)
    {
        lua_newtable(state);
        const int safe = lua_gettop(state);
        lua_pushglobaltable(state);
        lua_pushnil(state);
        while (lua_next(state, -2))
        {
            // Tables are copied directly into each instance; omitting them
            // here prevents nil/removal in an instance revealing a shared table.
            const char* key = lua_type(state, -2) == LUA_TSTRING ? lua_tostring(state, -2) : nullptr;
            if (!lua_istable(state, -1) && (!key || std::strcmp(key, "load") != 0))
            {
                lua_pushvalue(state, -2);
                lua_pushvalue(state, -2);
                lua_rawset(state, safe);
            }
            lua_pop(state, 1);
        }
        lua_pop(state, 1);
        lua_pushcfunction(state, InstanceGetMetatable);
        lua_setfield(state, safe, "getmetatable");
        // Registry growth may allocate: keep it inside this protected frame.
        const int reference = luaL_ref(state, LUA_REGISTRYINDEX);
        lua_pushinteger(state, reference);
        return 1;
    }

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
            const char* error = lua_type(state_, -1) == LUA_TSTRING ? lua_tostring(state_, -1) : nullptr;
            throw std::runtime_error(error ? error : "library initialization failed");
        }
        bindings_.InstallAll(state_);
        lua_pushcfunction(state_, CreateRegistryThread);
        if (lua_pcall(state_, 0, 2, 0) != LUA_OK)
        {
            const char* error = lua_type(state_, -1) == LUA_TSTRING ? lua_tostring(state_, -1) : nullptr;
            throw std::runtime_error(error ? error : "registry cleanup initialization failed");
        }
        registryThread_ = static_cast<int>(lua_tointeger(state_, -2));
        registryState_ = static_cast<lua_State*>(lua_touserdata(state_, -1));
        lua_pop(state_, 2);
        lua_pushcfunction(state_, BuildSafeGlobals);
        if (lua_pcall(state_, 0, 1, 0) != LUA_OK)
        {
            const char* error = lua_type(state_, -1) == LUA_TSTRING ? lua_tostring(state_, -1) : nullptr;
            throw std::runtime_error(error ? error : "safe-global initialization failed");
        }
        safeGlobals_ = static_cast<int>(lua_tointeger(state_, -1));
        lua_pop(state_, 1);
        cache_ = std::make_unique<FLuaScriptCache>(state_, projectRoot_);
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
    // Instances may be held by components through unique_ptr. Invalidate those
    // handles and release their references before closing the borrowed VM.
    while (!instances_.empty()) (*instances_.begin())->Release();
    cache_.reset();
    // Only an early Init failure can lack this private stack; in that case no
    // subsystem registry references have been created yet.
    if (registryState_)
    {
        luaL_unref(registryState_, LUA_REGISTRYINDEX, safeGlobals_);
        luaL_unref(registryState_, LUA_REGISTRYINDEX, registryThread_);
    }
    safeGlobals_ = LUA_NOREF;
    registryThread_ = LUA_NOREF;
    registryState_ = nullptr;
    lua_State* closing = state_;
    state_ = nullptr;
    lua_close(closing);
}

FLuaBindingRegistry& UScriptSubsystem::Bindings() { return bindings_; }
lua_State* UScriptSubsystem::State() const noexcept { return state_; }

void UScriptSubsystem::SetProjectRoot(std::filesystem::path projectRoot)
{
    if (!instances_.empty()) throw std::logic_error("cannot change Lua project root while instances are live");
    auto root = std::filesystem::canonical(projectRoot);
    if (!std::filesystem::is_directory(root)) throw std::invalid_argument("Lua project root must be a directory");
    auto cache = state_ ? std::make_unique<FLuaScriptCache>(state_, root) : nullptr;
    projectRoot_ = std::move(root);
    cache_ = std::move(cache);
}

std::shared_ptr<const FLuaScriptAsset> UScriptSubsystem::LoadScriptAsset(const std::filesystem::path& path)
{
    try
    {
        if (!cache_) throw std::runtime_error("script subsystem is not initialized");
        return cache_->Load(path);
    }
    catch (const std::exception& error)
    {
        try { logSink_("Lua load [" + path.generic_string() + "] [asset]: " + error.what()); }
        catch (...) {}
    }
    catch (...) {}
    return {};
}

std::unique_ptr<FLuaScriptInstance> UScriptSubsystem::CreateScriptInstance(
    const std::filesystem::path& path, std::string diagnosticOwner)
{
    try
    {
        if (!cache_) throw std::runtime_error("script subsystem is not initialized");
        auto instance = std::unique_ptr<FLuaScriptInstance>(new FLuaScriptInstance(
            this, state_, cache_->Load(path), diagnosticOwner, logSink_));
        instances_.insert(instance.get());
        if (!instance->Initialize(safeGlobals_)) return {};
        return instance;
    }
    catch (const std::exception& error)
    {
        try { logSink_("Lua load [" + path.generic_string() + "] [" + diagnosticOwner + "]: " + error.what()); }
        catch (...) {}
    }
    catch (...)
    {
        try { logSink_("Lua load [" + path.generic_string() + "] [" + diagnosticOwner + "]: unknown native exception"); }
        catch (...) {}
    }
    return {};
}

void UScriptSubsystem::ClearScriptCache()
{
    if (!instances_.empty()) throw std::logic_error("cannot clear Lua cache while instances are live");
    if (cache_) cache_->Clear();
}

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
