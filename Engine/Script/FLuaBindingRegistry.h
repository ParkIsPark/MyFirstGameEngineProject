#pragma once

#include <functional>
#include <string>
#include <vector>

struct lua_State;
using FLuaBindingInstaller = std::function<void(lua_State*)>;

// Native modules register before subsystem Init. Each installer receives the
// same VM in registration order; its temporary Lua stack values are discarded.
class FLuaBindingRegistry
{
public:
    void Register(std::string name, FLuaBindingInstaller installer);
    // Throws a contextual runtime_error on Lua/native installer failure.
    // Earlier installers may have changed the VM; startup discards it on failure.
    void InstallAll(lua_State* state) const;

private:
    struct Entry
    {
        std::string name;
        FLuaBindingInstaller installer;
    };
    std::vector<Entry> entries_;
};
