#include "FLuaBindingRegistry.h"
#include "LuaInclude.h"

#include <stdexcept>
#include <utility>

namespace
{
    struct InstallContext
    {
        const FLuaBindingInstaller& installer;
        std::string nativeError;
    };

    int InstallProtected(lua_State* state)
    {
        auto* context = static_cast<InstallContext*>(lua_touserdata(state, 1));
        lua_settop(state, 0);
        try
        {
            context->installer(state);
            return 0;
        }
        catch (const std::exception& error) { context->nativeError = error.what(); }
        catch (...) { context->nativeError = "unknown native exception"; }
        // All C++ exception scopes have ended before Lua can longjmp.
        lua_pushlstring(state, context->nativeError.data(), context->nativeError.size());
        return lua_error(state);
    }
}

void FLuaBindingRegistry::Register(std::string name, FLuaBindingInstaller installer)
{
    if (name.empty() || !installer)
        throw std::invalid_argument("Lua binding requires a name and installer");
    for (const auto& entry : entries_)
        if (entry.name == name)
            throw std::invalid_argument("Duplicate Lua binding: " + name);
    entries_.push_back({std::move(name), std::move(installer)});
}

void FLuaBindingRegistry::InstallAll(lua_State* state) const
{
    if (!state) throw std::invalid_argument("Lua bindings require a non-null state");
    const int top = lua_gettop(state);
    for (const auto& entry : entries_)
    {
        InstallContext context{entry.installer, {}};
        lua_pushcfunction(state, InstallProtected);
        lua_pushlightuserdata(state, &context);
        if (lua_pcall(state, 1, 0, 0) != LUA_OK)
        {
            const char* detail = lua_tostring(state, -1);
            const std::string message = "Lua binding '" + entry.name + "': " +
                (detail ? detail : "non-string Lua error");
            lua_settop(state, top);
            throw std::runtime_error(message);
        }
        lua_settop(state, top);
    }
}
