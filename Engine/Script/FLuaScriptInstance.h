#pragma once

#include "FLuaScriptAsset.h"
#include <functional>
#include <memory>
#include <string>
#include <string_view>

struct lua_State;
class UScriptSubsystem;

// Owns registry references on the subsystem VM. Shutdown invalidates outstanding
// handles before closing Lua; subsequent lifecycle calls fail safely.
class FLuaScriptInstance
{
public:
    ~FLuaScriptInstance();
    FLuaScriptInstance(const FLuaScriptInstance&) = delete;
    FLuaScriptInstance& operator=(const FLuaScriptInstance&) = delete;
    bool BeginPlay() noexcept;
    bool Tick(float deltaSeconds) noexcept;
    void EndPlay() noexcept;
    bool HasFunction(std::string_view name) const;
    const std::shared_ptr<const FLuaScriptAsset>& Asset() const noexcept { return asset_; }

private:
    friend class UScriptSubsystem;
    FLuaScriptInstance(UScriptSubsystem* owner, lua_State* state,
        std::shared_ptr<const FLuaScriptAsset> asset, std::string diagnosticOwner,
        std::function<void(std::string_view)> errorSink);
    bool Initialize(int safeGlobals);
    void Release() noexcept;
    bool Call(int callback, const char* phase, bool hasDelta, float delta) noexcept;
    void Report(const char* phase, const char* detail) noexcept;
    static int InitializeProtected(lua_State* state);

    UScriptSubsystem* owner_;
    lua_State* state_;
    std::shared_ptr<const FLuaScriptAsset> asset_;
    std::string diagnosticOwner_;
    std::function<void(std::string_view)> errorSink_;
    int environment_ = -2; // LUA_NOREF, kept private to preserve ownership.
    int callbacks_[3] = {-2, -2, -2};
};
