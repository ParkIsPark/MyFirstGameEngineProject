#include "FLuaBindingRegistry.h"
#include "LuaInclude.h"

#include <cstdio>
#include <stdexcept>
#include <utility>

namespace
{
    struct InstallContext
    {
        const FLuaBindingInstaller& installer;
        char nativeError[1024] = {};
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
        catch (const std::exception& error)
        {
            std::snprintf(context->nativeError, sizeof(context->nativeError), "%s", error.what());
        }
        catch (...)
        {
            std::snprintf(context->nativeError, sizeof(context->nativeError), "unknown native exception");
        }
        // Conversion cannot allocate/throw inside a catch handler. All C++
        // exception scopes have ended before Lua allocation can longjmp.
        lua_pushstring(state, context->nativeError);
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
