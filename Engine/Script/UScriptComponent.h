#pragma once

#include "../World/UActorComponent.h"
#include <filesystem>
#include <memory>

class FArchive;
class UWorld;
class UScriptSubsystem;
class FLuaScriptInstance;

// Explicit loader registration keeps the concrete component object reachable
// when a consumer links the engine as a static library.
void RegisterScriptComponentType();

// Serializable Actor attachment selecting one project-relative Lua asset.
// The VM belongs to UScriptSubsystem; this attachment owns its runtime handle.
class UScriptComponent final : public UActorComponent
{
public:
    UScriptComponent();
    ~UScriptComponent() override;
    UScriptComponent(const UScriptComponent& other);
    UScriptComponent& operator=(const UScriptComponent&) = delete;
    std::string_view TypeName() const override { return "ScriptComponent"; }
    const std::filesystem::path& ScriptPath() const noexcept { return scriptPath_; }
    void SetScriptPath(std::filesystem::path projectRelativePath);
    void ClearScriptPath() noexcept;
    void Serialize(FArchive& ar) override;
    void BeginPlay() noexcept override;
    void Tick(float deltaSeconds) noexcept override;
    void EndPlay() noexcept override;
    bool RequiresEndPlay() const noexcept override { return subsystem_ != nullptr || begun_; }

private:
    friend class UWorld;
    void ConfigureRuntime(UScriptSubsystem* subsystem) noexcept;
    std::filesystem::path scriptPath_;
    UScriptSubsystem* subsystem_ = nullptr;
    std::unique_ptr<FLuaScriptInstance> instance_;
    bool attempted_ = false;
    bool begun_ = false;
    bool callbackActive_ = false;
    bool stopRequested_ = false;
};
