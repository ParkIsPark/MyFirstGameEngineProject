#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class AActor;
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
