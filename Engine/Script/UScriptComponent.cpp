#include "UScriptComponent.h"

#include "FScriptPath.h"
#include "FArchive.h"

REGISTER_COMPONENT("ScriptComponent", UScriptComponent)

void UScriptComponent::SetScriptPath(std::filesystem::path projectRelativePath)
{
    const std::filesystem::path normalized = NormalizeScriptProjectPath(projectRelativePath);
    scriptPath_ = normalized;
}

void UScriptComponent::Serialize(FArchive& ar)
{
    const bool priorEnabled = IsEnabled();
    UActorComponent::Serialize(ar);
    std::string serialized = scriptPath_.generic_string();
    ar.Field("Script", serialized);
    if (ar.IsLoading() && !serialized.empty())
    {
        try { SetScriptPath(serialized); } // validates before replacing the prior path
        catch (...)
        {
            SetEnabled(priorEnabled);
            throw;
        }
    }
}
