#include "FEditorAssetWorkflow.h"

#include "../Script/FScriptPath.h"
#include "../Script/UScriptComponent.h"
#include "../World/AActor.h"

#include <cctype>
#include <stdexcept>
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

    FEditorScriptAssignment Reject(std::string message)
    {
        FEditorScriptAssignment result;
        result.validationMessage = std::move(message);
        return result;
    }
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
    fs::path copiedDestination;
    if (selectedFile.empty()) return Reject("Choose a Lua file.");
    if (Lower(selectedFile.extension().string()) != ".lua")
        return Reject("Script assets must use the .lua extension.");

    std::error_code ec;
    if (!fs::exists(selectedFile, ec) || ec) return Reject("The selected Lua file does not exist.");
    if (!fs::is_regular_file(selectedFile, ec) || ec) return Reject("The selected Lua path is not a file.");

    const fs::path lexicalSource = fs::absolute(selectedFile, ec).lexically_normal();
    if (ec) return Reject("Could not resolve the selected Lua file: " + ec.message());
    if (HasAnyReparsePoint(lexicalSource))
        return Reject("Script aliases and reparse-point files are not supported.");

    const fs::path source = fs::weakly_canonical(selectedFile, ec);
    if (ec) return Reject("Could not resolve the selected Lua file: " + ec.message());
    if (Lower(source.extension().string()) != ".lua")
        return Reject("The resolved script target must use the .lua extension.");
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
    if (!IsStrictlyWithinPhysicalRoot(scriptsRoot, canonicalContent))
        return Reject("Content/Scripts resolves outside the project Content directory.");

    fs::path projectRelative;
    fs::path copySource;
    fs::path copyDestination;
    if (IsWithin(source, scriptsRoot))
    {
        const fs::path contentRelative = source.lexically_relative(canonicalContent);
        if (contentRelative.empty()) return Reject("Could not make the Lua path project-relative.");
        projectRelative = fs::path("Content") / contentRelative;
    }
    else
    {
        copySource = source;
        copyDestination = scriptsRoot / lexicalSource.filename();
        if (fs::exists(copyDestination, ec))
            return Reject("A script named '" + lexicalSource.filename().string() + "' already exists in Content/Scripts.");
        projectRelative = fs::path("Content/Scripts") / lexicalSource.filename();
    }

    FEditorScriptAssignment result;
    try
    {
        // Validate the exact serialized identity before copying or changing the
        // component. A reparse may never turn Alias.lua into Payload.txt here.
        projectRelative = NormalizeScriptProjectPath(projectRelative);
        if (Lower(projectRelative.extension().string()) != ".lua")
            throw std::invalid_argument("The assigned script path must use the .lua extension.");
        if (!copySource.empty())
        {
            ec.clear();
            if (!fs::copy_file(copySource, copyDestination, fs::copy_options::none, ec) || ec)
                throw std::runtime_error("Could not copy the Lua file into Content/Scripts: " + ec.message());
            copiedDestination = copyDestination;
        }
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
        if (!copiedDestination.empty())
        {
            std::error_code rollbackError;
            fs::remove(copiedDestination, rollbackError);
        }
        return Reject(error.what());
    }
    return result;
}
