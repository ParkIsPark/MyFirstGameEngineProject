#pragma once

#include "../World/UActorComponent.h"
#include <filesystem>

class FArchive;

// Serializable Actor attachment selecting one project-relative Lua asset.
// Runtime Lua state belongs to UScriptSubsystem, never to this component.
class UScriptComponent final : public UActorComponent
{
public:
    std::string_view TypeName() const override { return "ScriptComponent"; }
    const std::filesystem::path& ScriptPath() const noexcept { return scriptPath_; }
    void SetScriptPath(std::filesystem::path projectRelativePath);
    void Serialize(FArchive& ar) override;

private:
    std::filesystem::path scriptPath_;
};
