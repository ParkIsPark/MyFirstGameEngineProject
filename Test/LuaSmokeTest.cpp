// LuaSmokeTest.cpp -- standalone smoke test for the embedded Lua runtime.
//
// MSYS2 UCRT64:
// gcc -std=c17 -I./ThirdParty/Lua/5.4.9/src -c ThirdParty/Lua/5.4.9/src/lapi.c ThirdParty/Lua/5.4.9/src/lauxlib.c ThirdParty/Lua/5.4.9/src/lbaselib.c ThirdParty/Lua/5.4.9/src/lcode.c ThirdParty/Lua/5.4.9/src/lcorolib.c ThirdParty/Lua/5.4.9/src/lctype.c ThirdParty/Lua/5.4.9/src/ldebug.c ThirdParty/Lua/5.4.9/src/ldo.c ThirdParty/Lua/5.4.9/src/ldump.c ThirdParty/Lua/5.4.9/src/lfunc.c ThirdParty/Lua/5.4.9/src/lgc.c ThirdParty/Lua/5.4.9/src/llex.c ThirdParty/Lua/5.4.9/src/lmathlib.c ThirdParty/Lua/5.4.9/src/lmem.c ThirdParty/Lua/5.4.9/src/lobject.c ThirdParty/Lua/5.4.9/src/lopcodes.c ThirdParty/Lua/5.4.9/src/lparser.c ThirdParty/Lua/5.4.9/src/lstate.c ThirdParty/Lua/5.4.9/src/lstring.c ThirdParty/Lua/5.4.9/src/lstrlib.c ThirdParty/Lua/5.4.9/src/ltable.c ThirdParty/Lua/5.4.9/src/ltablib.c ThirdParty/Lua/5.4.9/src/ltm.c ThirdParty/Lua/5.4.9/src/lundump.c ThirdParty/Lua/5.4.9/src/lutf8lib.c ThirdParty/Lua/5.4.9/src/lvm.c ThirdParty/Lua/5.4.9/src/lzio.c && g++ -std=c++17 -Wall -Wextra -I./Engine/Script -I./ThirdParty/Lua/5.4.9/src Test/LuaSmokeTest.cpp lapi.o lauxlib.o lbaselib.o lcode.o lcorolib.o lctype.o ldebug.o ldo.o ldump.o lfunc.o lgc.o llex.o lmathlib.o lmem.o lobject.o lopcodes.o lparser.o lstate.o lstring.o lstrlib.o ltable.o ltablib.o ltm.o lundump.o lutf8lib.o lvm.o lzio.o -o LuaSmokeTest.exe && ./LuaSmokeTest.exe

#include "LuaInclude.h"

#include <cstdio>

namespace
{
    struct LuaStateCloser
    {
        lua_State* State;

        ~LuaStateCloser()
        {
            if (State != nullptr)
            {
                lua_close(State);
            }
        }
    };

    int Fail(const char* Message)
    {
        std::fprintf(stderr, "%s\n", Message);
        return 1;
    }

    void OpenLibrary(lua_State* State, const char* Name, lua_CFunction OpenFunction)
    {
        luaL_requiref(State, Name, OpenFunction, 1);
        lua_pop(State, 1);
    }

    void RemoveGlobal(lua_State* State, const char* Name)
    {
        lua_pushnil(State);
        lua_setglobal(State, Name);
    }

    bool IsGlobalAbsent(lua_State* State, const char* Name)
    {
        lua_getglobal(State, Name);
        const bool IsAbsent = lua_isnil(State, -1) != 0;
        lua_pop(State, 1);
        return IsAbsent;
    }
}

int main()
{
    lua_State* State = luaL_newstate();
    if (State == nullptr)
    {
        return Fail("luaL_newstate failed");
    }
    LuaStateCloser StateCloser{ State };

    OpenLibrary(State, LUA_GNAME, luaopen_base);
    OpenLibrary(State, LUA_COLIBNAME, luaopen_coroutine);
    OpenLibrary(State, LUA_TABLIBNAME, luaopen_table);
    OpenLibrary(State, LUA_STRLIBNAME, luaopen_string);
    OpenLibrary(State, LUA_MATHLIBNAME, luaopen_math);
    OpenLibrary(State, LUA_UTF8LIBNAME, luaopen_utf8);

    // Lua's base library provides these file-loading helpers, so the embedded
    // runtime explicitly leaves them unavailable alongside the unopened libs.
    RemoveGlobal(State, "dofile");
    RemoveGlobal(State, "loadfile");

    if (luaL_loadstring(State, "return 6 * 7") != LUA_OK || lua_pcall(State, 0, 1, 0) != LUA_OK)
    {
        return Fail("Lua failed to evaluate return 6 * 7");
    }
    if (!lua_isinteger(State, -1) || lua_tointeger(State, -1) != 42)
    {
        return Fail("Lua expression result was not integer 42");
    }
    lua_pop(State, 1);

    const char* UnsafeGlobals[] = { "io", "os", "package", "debug", "dofile", "loadfile" };
    for (const char* Name : UnsafeGlobals)
    {
        if (!IsGlobalAbsent(State, Name))
        {
            return Fail("unsafe Lua global was available");
        }
    }

    return 0;
}
