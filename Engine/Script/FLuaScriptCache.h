#pragma once

#include "FLuaScriptAsset.h"
#include <map>
#include <memory>

struct lua_State;

// Borrowed VM must outlive the cache. Engine-facing code uses UScriptSubsystem.
class FLuaScriptCache
{
public:
    FLuaScriptCache(lua_State* state, std::filesystem::path projectRoot);
    std::shared_ptr<const FLuaScriptAsset> Load(const std::filesystem::path& projectRelativePath);
    void Clear();

private:
    lua_State* state_;
    std::filesystem::path projectRoot_;
    std::map<std::filesystem::path, std::shared_ptr<const FLuaScriptAsset>> assets_;
};
