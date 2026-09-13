#include "UScriptComponent.h"

#include "FScriptPath.h"
#include "FArchive.h"

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
