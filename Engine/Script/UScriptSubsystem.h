#pragma once

#include "../Framework/USubsystem.h"
#include "FLuaBindingRegistry.h"
#include "FLuaScriptCache.h"
#include "FLuaScriptInstance.h"
#include <set>
#include <string_view>

using FLuaLogSink = std::function<void(std::string_view)>;

// Engine-owned VM. State() is borrowed by internal script services only;
// callers must not close it or retain it past Shutdown().
class UScriptSubsystem final : public USubsystem
{
public:
    explicit UScriptSubsystem(FLuaLogSink logSink = {});
    ~UScriptSubsystem() override;
    UScriptSubsystem(const UScriptSubsystem&) = delete;
    UScriptSubsystem& operator=(const UScriptSubsystem&) = delete;

    void Init() override;
    void Shutdown() override;
    FLuaBindingRegistry& Bindings();
    lua_State* State() const noexcept;
    void SetProjectRoot(std::filesystem::path projectRoot);
    std::shared_ptr<const FLuaScriptAsset> LoadScriptAsset(const std::filesystem::path& path);
    std::unique_ptr<FLuaScriptInstance> CreateScriptInstance(
        const std::filesystem::path& path, std::string diagnosticOwner);
    void ClearScriptCache();

private:
    friend class FLuaScriptInstance;
    static int Log(lua_State* state);
    void ReportInitFailure(std::string_view detail) noexcept;

    FLuaBindingRegistry bindings_;
    FLuaLogSink logSink_;
    lua_State* state_ = nullptr;
    std::filesystem::path projectRoot_ = ".";
    std::unique_ptr<FLuaScriptCache> cache_;
    std::set<FLuaScriptInstance*> instances_;
    int safeGlobals_ = -2;
};
