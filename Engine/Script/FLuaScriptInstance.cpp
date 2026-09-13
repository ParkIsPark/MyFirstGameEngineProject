#include "FLuaScriptInstance.h"
#include "UScriptSubsystem.h"
#include "LuaInclude.h"

#include <utility>

namespace
{
    const char* const CallbackNames[] = {"BeginPlay", "Tick", "EndPlay"};

    // All functions called inside Lua's protected frame use only trivial local
    // values; Lua allocation errors may longjmp, never bypass C++ destructors.
    void CloneTable(lua_State* state, int source, int copies)
    {
        if (!lua_checkstack(state, 8)) luaL_error(state, "library table nesting exceeds Lua stack");
        source = lua_absindex(state, source);
        copies = lua_absindex(state, copies);
        lua_pushvalue(state, source);
        lua_rawget(state, copies);
        if (!lua_isnil(state, -1)) return;
        lua_pop(state, 1);
        lua_newtable(state);
        const int clone = lua_gettop(state);
        lua_pushvalue(state, source);
        lua_pushvalue(state, clone);
        lua_rawset(state, copies);
        lua_pushnil(state);
        while (lua_next(state, source))
        {
            if (lua_istable(state, -2)) CloneTable(state, -2, copies);
            else lua_pushvalue(state, -2);
            if (lua_istable(state, -2)) CloneTable(state, -2, copies);
            else lua_pushvalue(state, -2);
            lua_rawset(state, clone);
            lua_pop(state, 1);
        }
    }
}

FLuaScriptInstance::FLuaScriptInstance(UScriptSubsystem* owner, lua_State* state,
    std::shared_ptr<const FLuaScriptAsset> asset, std::string diagnosticOwner,
    std::function<void(std::string_view)> errorSink)
    : owner_(owner), state_(state), asset_(std::move(asset)),
      diagnosticOwner_(std::move(diagnosticOwner)), errorSink_(std::move(errorSink)) {}

FLuaScriptInstance::~FLuaScriptInstance() { Release(); }

bool FLuaScriptInstance::Initialize(int safeGlobals)
{
    const int top = lua_gettop(state_);
    if (!lua_checkstack(state_, 3))
    {
        Report("top-level", "cannot grow Lua stack for instance initialization");
        return false;
    }
    lua_pushcfunction(state_, InitializeProtected);
    lua_pushlightuserdata(state_, this);
    lua_rawgeti(state_, LUA_REGISTRYINDEX, safeGlobals);
    const int result = lua_pcall(state_, 2, 0, 0);
    if (result != LUA_OK)
    {
        const char* detail = lua_type(state_, -1) == LUA_TSTRING ? lua_tostring(state_, -1) : nullptr;
        Report("top-level", detail ? detail : "non-string Lua error");
    }
    lua_settop(state_, top);
    return result == LUA_OK;
}

int FLuaScriptInstance::InitializeProtected(lua_State* state)
{
    auto* instance = static_cast<FLuaScriptInstance*>(lua_touserdata(state, 1));
    // Keep the subsystem's safe scalar/function globals on the stack at index 2.
    if (luaL_loadbufferx(state,
        reinterpret_cast<const char*>(instance->asset_->compiledChunk.data()),
        instance->asset_->compiledChunk.size(), "cached script", "b") != LUA_OK)
        return lua_error(state);
    const int chunk = lua_gettop(state);
    lua_newtable(state);
    const int environment = lua_gettop(state);
    lua_pushvalue(state, environment);
    lua_setfield(state, environment, "_G");
    lua_newtable(state);
    const int copies = lua_gettop(state);
    lua_pushglobaltable(state);
    const int globals = lua_gettop(state);
    // Register _G's copy before recursively cloning module tables with cycles.
    lua_pushvalue(state, globals);
    lua_pushvalue(state, environment);
    lua_rawset(state, copies);
    lua_pushnil(state);
    while (lua_next(state, globals))
    {
        if (lua_istable(state, -1) && !lua_rawequal(state, -1, globals))
        {
            lua_pushvalue(state, -2);
            CloneTable(state, -2, copies);
            lua_rawset(state, environment);
        }
        lua_pop(state, 1);
    }
    lua_pop(state, 2); // globals, clone map
    lua_newtable(state);
    lua_pushvalue(state, 2);
    lua_setfield(state, -2, "__index");
    lua_pushboolean(state, 0);
    lua_setfield(state, -2, "__metatable");
    lua_setmetatable(state, environment);
    lua_pushvalue(state, environment);
    instance->environment_ = luaL_ref(state, LUA_REGISTRYINDEX);
    lua_pushvalue(state, environment);
    if (!lua_setupvalue(state, chunk, 1))
        return luaL_error(state, "cached chunk has no _ENV upvalue");
    lua_pushvalue(state, chunk);
    lua_call(state, 0, 0); // protected by Initialize's pcall
    for (int index = 0; index != 3; ++index)
    {
        lua_pushstring(state, CallbackNames[index]);
        lua_rawget(state, environment);
        if (lua_isnil(state, -1)) { lua_pop(state, 1); continue; }
        if (!lua_isfunction(state, -1))
            return luaL_error(state, "%s callback must be a function", CallbackNames[index]);
        instance->callbacks_[index] = luaL_ref(state, LUA_REGISTRYINDEX);
    }
    return 0;
}

void FLuaScriptInstance::Release() noexcept
{
    if (state_)
    {
        // A private subsystem thread shares this VM's registry and always has
        // scratch space. Cleanup must not depend on growing an occupied caller
        // stack, because destructors must also work under allocation failure.
        lua_State* registry = owner_->registryState_;
        for (int& callback : callbacks_)
        {
            luaL_unref(registry, LUA_REGISTRYINDEX, callback);
            callback = LUA_NOREF;
        }
        luaL_unref(registry, LUA_REGISTRYINDEX, environment_);
        environment_ = LUA_NOREF;
        state_ = nullptr;
    }
    if (owner_)
    {
        owner_->instances_.erase(this);
        owner_ = nullptr;
    }
}

bool FLuaScriptInstance::Call(int callback, const char* phase, bool hasDelta, float delta) noexcept
{
    if (!state_) return false;
    if (callback == LUA_NOREF) return true;
    const int top = lua_gettop(state_);
    if (!lua_checkstack(state_, 2)) { Report(phase, "cannot grow Lua stack"); return false; }
    lua_rawgeti(state_, LUA_REGISTRYINDEX, callback);
    if (hasDelta) lua_pushnumber(state_, static_cast<lua_Number>(delta));
    const int status = lua_pcall(state_, hasDelta ? 1 : 0, 0, 0);
    if (status != LUA_OK)
    {
        const char* detail = lua_type(state_, -1) == LUA_TSTRING ? lua_tostring(state_, -1) : nullptr;
        Report(phase, detail ? detail : "non-string Lua error");
    }
    lua_settop(state_, top);
    return status == LUA_OK;
}

bool FLuaScriptInstance::BeginPlay() noexcept { return Call(callbacks_[0], "BeginPlay", false, 0); }
bool FLuaScriptInstance::Tick(float delta) noexcept { return Call(callbacks_[1], "Tick", true, delta); }
void FLuaScriptInstance::EndPlay() noexcept { Call(callbacks_[2], "EndPlay", false, 0); }

bool FLuaScriptInstance::HasFunction(std::string_view name) const
{
    for (int index = 0; index != 3; ++index)
        if (name == CallbackNames[index]) return state_ && callbacks_[index] != LUA_NOREF;
    return false;
}

void FLuaScriptInstance::Report(const char* phase, const char* detail) noexcept
{
    try
    {
        errorSink_("Lua " + diagnosticOwner_ + " Path=\"" +
            asset_->projectRelativePath.generic_string() + "\" Phase=" + phase + ": " + detail);
    }
    catch (...) {} // Diagnostic sinks cannot escape a lifecycle boundary.
}
