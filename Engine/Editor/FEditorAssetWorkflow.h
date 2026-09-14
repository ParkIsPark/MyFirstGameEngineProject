#pragma once

#include "Material.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class AActor;
class UMeshComponent;
class UScriptComponent;

// Filesystem/editor operations kept independent from ImGui and OpenGL so the
// real authoring behavior can be exercised by focused tests.
struct FEditorContentAsset
{
    std::filesystem::path relativePath; // relative to the active Content root
    std::string category;
    std::string icon;
};

struct FEditorScriptAssignment
{
    bool succeeded = false;
    bool changed = false;
    std::filesystem::path projectRelativePath;
    std::string validationMessage;
};

std::vector<FEditorContentAsset> DiscoverEditorContent(const std::filesystem::path& contentRoot);
std::filesystem::path ResolveEditorContentPath(
    const std::filesystem::path& contentRoot, const FEditorContentAsset& asset);
std::filesystem::path CopyEditorAssetToContent(
    const std::filesystem::path& selectedFile,
    const std::filesystem::path& contentRoot,
    bool& copied,
    std::string& error);
bool RenameEditorContentAsset(
    const std::filesystem::path& contentRoot,
    const FEditorContentAsset& asset,
    const std::filesystem::path& newLeaf,
    std::string& error);
bool DeleteEditorContentAsset(
    const std::filesystem::path& contentRoot,
    const FEditorContentAsset& asset,
    std::string& error);
std::filesystem::path SelectEditorScriptAsset(
    const std::filesystem::path& contentRoot,
    const std::vector<FEditorContentAsset>& assets,
    size_t assetIndex);
std::filesystem::path EditorWorldNameFromPath(
    const std::filesystem::path& contentRoot, const std::filesystem::path& worldPath);
std::filesystem::path PrepareEditorWorldSavePath(
    const std::filesystem::path& contentRoot, const std::filesystem::path& contentRelativeStem);
UScriptComponent* ScriptComponentAt(AActor& actor, size_t componentIndex) noexcept;

// Accepts an existing in-project Lua asset or copies an external Lua file into
// Content/Scripts. The callback runs immediately before the component changes
// (EditorEngine supplies PushUndo). Rejected choices never mutate the component.
FEditorScriptAssignment AssignEditorLuaScript(
    UScriptComponent& component,
    const std::filesystem::path& selectedFile,
    const std::filesystem::path& contentRoot,
    const std::function<void()>& beforeChange);

// Payload-free authoring state used by the inspector. Material owns potentially
// large decoded texture pixels, which must not be copied simply to draw widgets.
struct FEditorMaterialDraft
{
    glm::vec3 ka = glm::vec3(0.2f);
    glm::vec3 kd = glm::vec3(1.0f);
    glm::vec3 ks = glm::vec3(0.0f);
    float shininess = 0.0f;
    glm::vec3 km = glm::vec3(0.0f);
    glm::vec3 emissive = glm::vec3(0.0f);
    std::string diffuseTexPath;
    EWrapMode wrapMode = EWrapMode::Repeat;
    glm::vec2 uvTiling = glm::vec2(1.0f);
    EMaterialBlendMode blendMode = EMaterialBlendMode::Opaque;
    float opacity = 1.0f;
    float refraction = 1.52f;
    glm::vec3 transmittanceColor = glm::vec3(1.0f);
    float transmittanceDistance = 1.0f;
    bool castRayTracedShadows = true;
};

FEditorMaterialDraft MakeEditorMaterialDraft(const Material& source);

// Commit authoring fields only after the caller captures pre-change state.
// Existing runtime texture payload remains resident unless reloadTexture is set.
// Component edits always become local overrides and never mutate their seed.
void CommitEditorMaterialEdit(Material& target, const FEditorMaterialDraft& edited,
                              bool reloadTexture, const std::function<void()>& beforeChange);
void CommitEditorComponentMaterialEdit(UMeshComponent& component,
                                       const FEditorMaterialDraft& edited,
                                       bool reloadTexture,
                                       const std::function<void()>& beforeChange);
