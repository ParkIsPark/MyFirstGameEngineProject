#include "FEditorAssetWorkflow.h"

#include "../Script/UScriptComponent.h"
#include "../World/AActor.h"

#include <algorithm>
#include <cctype>
#include <system_error>

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
            if (child == candidate.end() || *child != *parent) return false;
        return child != candidate.end();
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

    FEditorScriptAssignment Reject(std::string message)
    {
        FEditorScriptAssignment result;
        result.validationMessage = std::move(message);
        return result;
    }
}

std::vector<FEditorContentAsset> DiscoverEditorContent(const fs::path& contentRoot)
{
    std::vector<FEditorContentAsset> result;
    std::error_code ec;
    if (!fs::is_directory(contentRoot, ec)) return result;

    fs::recursive_directory_iterator current(contentRoot,
        fs::directory_options::skip_permission_denied, ec), end;
    while (!ec && current != end)
    {
        const fs::directory_entry entry = *current;
        current.increment(ec);
        std::error_code entryError;
        if (!entry.is_regular_file(entryError)) continue;
        fs::path relative = fs::relative(entry.path(), contentRoot, entryError);
        if (entryError || relative.empty()) continue;
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
    return (contentRoot / asset.relativePath).lexically_normal();
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

UScriptComponent* ScriptComponentAt(AActor& actor, size_t componentIndex) noexcept
{
    if (componentIndex >= actor.Components().size()) return nullptr;
    return dynamic_cast<UScriptComponent*>(actor.Components()[componentIndex].get());
}

FEditorScriptAssignment AssignEditorLuaScript(
    UScriptComponent& component,
    const fs::path& selectedFile,
    const fs::path& contentRoot,
    const std::function<void()>& beforeChange)
{
    if (selectedFile.empty()) return Reject("Choose a Lua file.");
    if (Lower(selectedFile.extension().string()) != ".lua")
        return Reject("Script assets must use the .lua extension.");

    std::error_code ec;
    if (!fs::exists(selectedFile, ec) || ec) return Reject("The selected Lua file does not exist.");
    if (!fs::is_regular_file(selectedFile, ec) || ec) return Reject("The selected Lua path is not a file.");

    const fs::path source = fs::weakly_canonical(selectedFile, ec);
    if (ec) return Reject("Could not resolve the selected Lua file: " + ec.message());
    fs::path projectRoot = contentRoot.parent_path();
    if (projectRoot.empty()) projectRoot = fs::current_path(ec);
    if (ec) return Reject("Could not resolve the project directory: " + ec.message());
    const fs::path canonicalProject = fs::weakly_canonical(projectRoot, ec);
    if (ec) return Reject("Could not resolve the project directory: " + ec.message());
    const fs::path canonicalContent = fs::weakly_canonical(contentRoot, ec);
    if (ec) return Reject("Could not resolve the Content directory: " + ec.message());
    if (!IsStrictlyWithinPhysicalRoot(canonicalContent, canonicalProject))
        return Reject("Content resolves outside the project directory.");

    fs::create_directories(contentRoot / "Scripts", ec);
    if (ec) return Reject("Could not create Content/Scripts: " + ec.message());
    const fs::path scriptsRoot = fs::weakly_canonical(contentRoot / "Scripts", ec);
    if (ec) return Reject("Could not resolve Content/Scripts: " + ec.message());
    // Copy through the resolved physical Scripts path only after proving both
    // Content and Scripts stayed inside their project-owned parents. This also
    // rejects junction/symlink escapes before any destination file is created.
    if (!IsStrictlyWithinPhysicalRoot(scriptsRoot, canonicalContent))
        return Reject("Content/Scripts resolves outside the project Content directory.");

    fs::path projectRelative;
    if (IsWithin(source, scriptsRoot))
    {
        const fs::path contentRelative = fs::relative(source, canonicalContent, ec);
        if (ec || contentRelative.empty()) return Reject("Could not make the Lua path project-relative.");
        projectRelative = fs::path("Content") / contentRelative;
    }
    else
    {
        const fs::path destination = scriptsRoot / source.filename();
        if (fs::exists(destination, ec))
            return Reject("A script named '" + source.filename().string() + "' already exists in Content/Scripts.");
        ec.clear();
        if (!fs::copy_file(source, destination, fs::copy_options::none, ec) || ec)
            return Reject("Could not copy the Lua file into Content/Scripts: " + ec.message());
        projectRelative = fs::path("Content/Scripts") / source.filename();
    }

    FEditorScriptAssignment result;
    try
    {
        projectRelative = projectRelative.lexically_normal();
        result.succeeded = true;
        result.projectRelativePath = projectRelative;
        result.changed = component.ScriptPath() != projectRelative;
        if (result.changed)
        {
            if (beforeChange) beforeChange();
            component.SetScriptPath(projectRelative);
        }
    }
    catch (const std::exception& error)
    {
        return Reject(error.what());
    }
    return result;
}
