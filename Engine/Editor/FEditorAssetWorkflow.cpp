#include "FEditorAssetWorkflow.h"
#include "Material.h"
#include "UMaterial.h"
#include "UMeshComponent.h"

#include <algorithm>
#include <cctype>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
    std::string Lower(std::string value)
    {
        for (char& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return value;
    }

    bool SamePathPart(const fs::path& left, const fs::path& right)
    {
#ifdef _WIN32
        return Lower(left.generic_string()) == Lower(right.generic_string());
#else
        return left == right;
#endif
    }

    bool IsWithin(const fs::path& candidate, const fs::path& root)
    {
        auto child = candidate.begin();
        for (auto parent = root.begin(); parent != root.end(); ++parent, ++child)
            if (child == candidate.end() || !SamePathPart(*child, *parent)) return false;
        return true;
    }

    bool IsStrictlyWithinPhysicalRoot(const fs::path& candidate, const fs::path& root)
    {
        auto child = candidate.begin();
        auto parent = root.begin();
        for (; parent != root.end(); ++parent, ++child)
            if (child == candidate.end() || !SamePathPart(*child, *parent)) return false;
        return child != candidate.end();
    }

    bool IsWithinPhysicalRoot(const fs::path& candidate, const fs::path& root)
    {
        auto child = candidate.begin();
        for (auto parent = root.begin(); parent != root.end(); ++parent, ++child)
            if (child == candidate.end() || !SamePathPart(*child, *parent)) return false;
        return true;
    }

    bool IsSafeRelative(const fs::path& path)
    {
        if (path.empty() || path.has_root_path() || path == ".") return false;
        for (const fs::path& part : path)
            if (part.empty() || part == "." || part == "..") return false;
        return true;
    }

    bool IsReparsePoint(const fs::path& path)
    {
#ifdef _WIN32
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
        std::error_code ec;
        return fs::is_symlink(fs::symlink_status(path, ec));
#endif
    }

    bool HasReparsePoint(const fs::path& root, const fs::path& candidate)
    {
        if (!IsWithin(candidate, root)) return true;
        fs::path current = root;
        if (IsReparsePoint(current)) return true;
        const fs::path relative = candidate.lexically_relative(root);
        for (const fs::path& part : relative)
        {
            current /= part;
            if (IsReparsePoint(current)) return true;
        }
        return false;
    }

    bool HasAnyReparsePoint(const fs::path& candidate)
    {
        fs::path current = candidate.root_path();
        for (const fs::path& part : candidate.relative_path())
        {
            current /= part;
            if (IsReparsePoint(current)) return true;
        }
        return false;
    }

    fs::path ResolveSafeContentEntry(const fs::path& contentRoot, const fs::path& relative)
    {
        if (!IsSafeRelative(relative)) return {};
        std::error_code ec;
        const fs::path root = fs::absolute(contentRoot, ec).lexically_normal();
        if (ec) return {};
        const fs::path candidate = (root / relative).lexically_normal();
        if (!IsWithin(candidate, root) || HasReparsePoint(root, candidate)) return {};
        const fs::path physicalRoot = fs::weakly_canonical(root, ec);
        if (ec) return {};
        const fs::path physicalCandidate = fs::weakly_canonical(candidate, ec);
        if (ec || !IsStrictlyWithinPhysicalRoot(physicalCandidate, physicalRoot)) return {};
        return candidate;
    }

    void Classify(const fs::path& path, std::string& category, std::string& icon)
    {
        const std::string ext = Lower(path.extension().string());
        category = "Other"; icon = "[?]";
        if      (ext == ".world")                         { category = "World";    icon = "[W]"; }
        else if (ext == ".obj" || ext == ".fbx")          { category = "Mesh";     icon = "[M]"; }
        else if (ext == ".material" || ext == ".mtl")     { category = "Material"; icon = "[Mat]"; }
        else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
                                                               { category = "Texture";  icon = "[T]"; }
        else if (ext == ".hdr")                           { category = "HDRI";     icon = "[H]"; }
        else if (ext == ".lua")                           { category = "Script";   icon = "[Lua]"; }
    }

}

namespace
{
    void ApplyEditorMaterialDraft(Material& target, const FEditorMaterialDraft& edited,
                                  bool reloadTexture)
    {
        target.ka = edited.ka;
        target.kd = edited.kd;
        target.ks = edited.ks;
        target.shininess = edited.shininess;
        target.km = edited.km;
        target.emissive = edited.emissive;
        target.diffuseTexPath = edited.diffuseTexPath;
        target.wrapMode = edited.wrapMode;
        target.uvTiling = edited.uvTiling;
        target.blendMode = edited.blendMode;
        target.opacity = edited.opacity;
        target.refraction = edited.refraction;
        target.transmittanceColor = edited.transmittanceColor;
        target.transmittanceDistance = edited.transmittanceDistance;
        target.castRayTracedShadows = edited.castRayTracedShadows;
        target.SanitizeOptics();
        if (reloadTexture) UMaterial::LoadTexture(target);
        else target.MarkRuntimeDirty();
    }
}

FEditorMaterialDraft MakeEditorMaterialDraft(const Material& source)
{
    FEditorMaterialDraft draft;
    draft.ka = source.ka;
    draft.kd = source.kd;
    draft.ks = source.ks;
    draft.shininess = source.shininess;
    draft.km = source.km;
    draft.emissive = source.emissive;
    draft.diffuseTexPath = source.diffuseTexPath;
    draft.wrapMode = source.wrapMode;
    draft.uvTiling = source.uvTiling;
    draft.blendMode = source.blendMode;
    draft.opacity = source.opacity;
    draft.refraction = source.refraction;
    draft.transmittanceColor = source.transmittanceColor;
    draft.transmittanceDistance = source.transmittanceDistance;
    draft.castRayTracedShadows = source.castRayTracedShadows;
    return draft;
}

void CommitEditorMaterialEdit(Material& target, const FEditorMaterialDraft& edited,
                              bool reloadTexture, const std::function<void()>& beforeChange)
{
    if (beforeChange) beforeChange();
    ApplyEditorMaterialDraft(target, edited, reloadTexture);
}

void CommitEditorComponentMaterialEdit(UMeshComponent& component,
                                       const FEditorMaterialDraft& edited,
                                       bool reloadTexture,
                                       const std::function<void()>& beforeChange)
{
    if (beforeChange) beforeChange();
    if (!component.hasMaterialOverride)
    {
        // The first real edit pays for exactly one copy into component ownership;
        // idle inspector frames and subsequent scalar edits never copy pixels.
        Material localOverride = component.GetMaterial();
        ApplyEditorMaterialDraft(localOverride, edited, reloadTexture);
        component.materialOverride = std::move(localOverride);
        component.hasMaterialOverride = true;
        return;
    }
    ApplyEditorMaterialDraft(component.materialOverride, edited, reloadTexture);
}

std::vector<FEditorContentAsset> DiscoverEditorContent(const fs::path& contentRoot)
{
    std::vector<FEditorContentAsset> result;
    std::error_code ec;
    if (!fs::is_directory(contentRoot, ec)) return result;

    const fs::path lexicalRoot = fs::absolute(contentRoot, ec).lexically_normal();
    if (ec || IsReparsePoint(lexicalRoot)) return result;

    fs::recursive_directory_iterator current(lexicalRoot,
        fs::directory_options::skip_permission_denied, ec), end;
    while (!ec && current != end)
    {
        const fs::directory_entry entry = *current;
        const bool reparse = IsReparsePoint(entry.path());
        if (reparse && entry.is_directory()) current.disable_recursion_pending();
        current.increment(ec);
        if (reparse) continue;
        std::error_code entryError;
        if (!entry.is_regular_file(entryError)) continue;
        fs::path relative = entry.path().lexically_normal().lexically_relative(lexicalRoot);
        if (!IsSafeRelative(relative) || ResolveSafeContentEntry(lexicalRoot, relative).empty()) continue;
        FEditorContentAsset asset;
        asset.relativePath = relative.lexically_normal();
        Classify(asset.relativePath, asset.category, asset.icon);
        result.push_back(std::move(asset));
    }

    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.relativePath.generic_string() < right.relativePath.generic_string();
    });
    return result;
}

fs::path ResolveEditorContentPath(const fs::path& contentRoot, const FEditorContentAsset& asset)
{
    return ResolveSafeContentEntry(contentRoot, asset.relativePath);
}

fs::path CopyEditorAssetToContent(
    const fs::path& selectedFile,
    const fs::path& contentRoot,
    bool& copied,
    std::string& error)
{
    copied = false;
    error.clear();
    std::error_code ec;
    if (selectedFile.empty() || !fs::is_regular_file(selectedFile, ec) || ec)
    {
        error = "The selected asset is not a regular file.";
        return {};
    }

    const fs::path source = fs::absolute(selectedFile, ec).lexically_normal();
    if (ec || HasAnyReparsePoint(source))
    {
        error = "The selected asset crosses a reparse point.";
        return {};
    }
    fs::create_directories(contentRoot, ec);
    if (ec)
    {
        error = "Could not create Content: " + ec.message();
        return {};
    }
    const fs::path root = fs::absolute(contentRoot, ec).lexically_normal();
    if (ec || IsReparsePoint(root))
    {
        error = "Could not resolve a safe Content directory.";
        return {};
    }
    const fs::path physicalRoot = fs::weakly_canonical(root, ec);
    if (ec)
    {
        error = "Could not resolve Content: " + ec.message();
        return {};
    }
    const fs::path physicalSource = fs::weakly_canonical(source, ec);
    if (ec)
    {
        error = "Could not resolve the selected asset: " + ec.message();
        return {};
    }

    if (IsStrictlyWithinPhysicalRoot(physicalSource, physicalRoot))
    {
        const fs::path relative = source.lexically_relative(root);
        if (!IsSafeRelative(relative) || ResolveSafeContentEntry(root, relative).empty())
        {
            error = "The selected Content asset has an unsafe identity.";
            return {};
        }
        return source;
    }

    const fs::path destination = root / source.filename();
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        error = "Could not copy the asset into Content: " + ec.message();
        return {};
    }
    copied = true;
    return destination;
}

bool RenameEditorContentAsset(
    const fs::path& contentRoot,
    const FEditorContentAsset& asset,
    const fs::path& newLeaf,
    std::string& error)
{
    error.clear();
    if (newLeaf.empty() || newLeaf != newLeaf.filename() || newLeaf == "." || newLeaf == "..")
    {
        error = "The new asset name must be one filename.";
        return false;
    }
    const fs::path source = ResolveSafeContentEntry(contentRoot, asset.relativePath);
    if (source.empty())
    {
        error = "The selected asset has an unsafe Content identity.";
        return false;
    }
    std::error_code ec;
    const fs::path root = fs::absolute(contentRoot, ec).lexically_normal();
    const fs::path relativeDestination = (asset.relativePath.parent_path() / newLeaf).lexically_normal();
    if (ec || !IsSafeRelative(relativeDestination))
    {
        error = "The renamed asset would leave Content.";
        return false;
    }
    const fs::path destination = (root / relativeDestination).lexically_normal();
    const fs::path parent = destination.parent_path();
    if (!IsWithin(destination, root) || HasReparsePoint(root, parent))
    {
        error = "The renamed asset would cross a reparse point.";
        return false;
    }
    const fs::path physicalRoot = fs::weakly_canonical(root, ec);
    const fs::path physicalParent = fs::weakly_canonical(parent, ec);
    if (ec || !IsWithinPhysicalRoot(physicalParent, physicalRoot))
    {
        error = "The renamed asset would leave Content.";
        return false;
    }
    if (fs::exists(destination, ec) || ec)
    {
        error = "An asset with that name already exists.";
        return false;
    }
    fs::rename(source, destination, ec);
    if (ec)
    {
        error = "Could not rename the asset: " + ec.message();
        return false;
    }
    return true;
}

bool DeleteEditorContentAsset(
    const fs::path& contentRoot,
    const FEditorContentAsset& asset,
    std::string& error)
{
    error.clear();
    const fs::path selected = ResolveSafeContentEntry(contentRoot, asset.relativePath);
    if (selected.empty())
    {
        error = "The selected asset has an unsafe Content identity.";
        return false;
    }
    std::error_code ec;
    if (!fs::is_regular_file(selected, ec) || ec || !fs::remove(selected, ec) || ec)
    {
        error = "Could not delete the selected Content asset.";
        return false;
    }
    return true;
}

fs::path SelectEditorScriptAsset(
    const fs::path& contentRoot,
    const std::vector<FEditorContentAsset>& assets,
    size_t assetIndex)
{
    if (assetIndex >= assets.size() || assets[assetIndex].category != "Script") return {};
    const fs::path selected = ResolveSafeContentEntry(contentRoot, assets[assetIndex].relativePath);
    if (selected.empty() || Lower(selected.extension().string()) != ".lua") return {};
    return selected;
}

fs::path EditorWorldNameFromPath(const fs::path& contentRoot, const fs::path& worldPath)
{
    std::error_code ec;
    const fs::path root = fs::absolute(contentRoot, ec).lexically_normal();
    if (ec) return worldPath.stem();
    const fs::path candidate = fs::absolute(worldPath, ec).lexically_normal();
    if (ec || !IsWithin(candidate, root)) return worldPath.stem();
    fs::path relative = candidate.lexically_relative(root);
    if (relative.empty()) return worldPath.stem();
    relative.replace_extension();
    return relative;
}

fs::path PrepareEditorWorldSavePath(const fs::path& contentRoot, const fs::path& contentRelativeStem)
{
    fs::path result = (contentRoot / contentRelativeStem).lexically_normal();
    result += ".world";
    std::error_code ec;
    fs::create_directories(result.parent_path(), ec);
    return result;
}
