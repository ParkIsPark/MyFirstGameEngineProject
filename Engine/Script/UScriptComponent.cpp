#include "UScriptComponent.h"

#include "FScriptPath.h"
#include "FArchive.h"

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
    // A sentinel distinguishes an omitted legacy field from an explicitly
    // present empty field, which must fail the same validation as SetScriptPath.
    static constexpr char kMissingScript[] = "\x1D";
    std::string serialized = ar.IsLoading() ? kMissingScript : scriptPath_.generic_string();
    ar.Field("Script", serialized);
    if (ar.IsLoading() && serialized != kMissingScript)
    {
        try { SetScriptPath(serialized); } // validates before replacing the prior path
        catch (...)
        {
            SetEnabled(priorEnabled);
            throw;
        }
    }
}
