#pragma once

#include "../Framework/USubsystem.h"
#include "FLuaBindingRegistry.h"
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

private:
    static int Log(lua_State* state);
    void ReportInitFailure(std::string_view detail) noexcept;

    FLuaBindingRegistry bindings_;
    FLuaLogSink logSink_;
    lua_State* state_ = nullptr;
};
