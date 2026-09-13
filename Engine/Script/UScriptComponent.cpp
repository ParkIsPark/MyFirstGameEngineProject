#include "UScriptComponent.h"

#include "FScriptPath.h"
#include "FArchive.h"
#include "UScriptSubsystem.h"
#include "../World/AActor.h"

UScriptComponent::UScriptComponent() = default;
UScriptComponent::~UScriptComponent() { EndPlay(); }
UScriptComponent::UScriptComponent(const UScriptComponent& other)
    : UActorComponent(other), scriptPath_(other.scriptPath_) {}

void UScriptComponent::ConfigureRuntime(UScriptSubsystem* subsystem) noexcept
{
    subsystem_ = subsystem;
}

void UScriptComponent::BeginPlay() noexcept
{
    if (!subsystem_ || attempted_ || callbackActive_ || !IsEnabled() || scriptPath_.empty()) return;
    attempted_ = true;
    callbackActive_ = true;
    try
    {
        const auto* actor = GetOwner();
        size_t index = 0;
        if (actor)
            for (const auto& component : actor->Components())
            {
                if (component.get() == this) break;
                ++index;
            }
        instance_ = subsystem_->CreateScriptInstance(scriptPath_,
            (actor ? actor->name : "(unowned)") + " ScriptComponent[" + std::to_string(index) + "]");
        if (instance_ && !stopRequested_ && instance_->BeginPlay()) begun_ = true;
        else instance_.reset();
    }
    catch (...) { instance_.reset(); }
    callbackActive_ = false;
    if (stopRequested_) EndPlay();
}

void UScriptComponent::Tick(float deltaSeconds) noexcept
{
    if (!begun_ || !instance_ || callbackActive_) return;
    callbackActive_ = true;
    const bool success = instance_->Tick(deltaSeconds);
    callbackActive_ = false;
    if (!success)
    {
        begun_ = false;
    }
    if (!success || stopRequested_) EndPlay();
}

void UScriptComponent::EndPlay() noexcept
{
    if (callbackActive_) { stopRequested_ = true; return; }
    // The handle must survive reentrant Stop from a callback's native sink.
    const bool end = begun_;
    begun_ = false;
    callbackActive_ = true;
    if (end && instance_) instance_->EndPlay();
    callbackActive_ = false;
    instance_.reset();
    attempted_ = false;
    subsystem_ = nullptr;
    stopRequested_ = false;
}

// Preserve registration for consumers that directly link this concrete type
// and query FComponentFactory before loading any world.
REGISTER_COMPONENT("ScriptComponent", UScriptComponent)

void RegisterScriptComponentType()
{
    static const bool registered = [] {
        FComponentFactory::Register("ScriptComponent", []() -> UActorComponent* {
            return new UScriptComponent();
        });
        return true;
    }();
    (void)registered;
}

void UScriptComponent::SetScriptPath(std::filesystem::path projectRelativePath)
{
    const std::filesystem::path normalized = NormalizeScriptProjectPath(projectRelativePath);
    scriptPath_ = normalized;
}

void UScriptComponent::Serialize(FArchive& ar)
{
    const bool priorEnabled = IsEnabled();
    UActorComponent::Serialize(ar);
    const bool hasScript = !ar.IsLoading() || ar.HasField("Script");
    std::string serialized = scriptPath_.generic_string();
    ar.Field("Script", serialized);
    if (ar.IsLoading() && hasScript)
    {
        try { SetScriptPath(serialized); } // validates before replacing the prior path
        catch (...)
        {
            SetEnabled(priorEnabled);
            throw;
        }
    }
}
