// Current complete recipe: powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name ScriptEditorWorkflowTest
// Prerequisite: Engine.sln Debug|Win32. Older direct compile examples follow.
// ScriptEditorWorkflowTest.cpp -- focused GL-free Task 7 coverage.
//
// MSYS2 UCRT64 filesystem-focused build/run (the macro selects the assertions
// whose exact production unit is FEditorAssetWorkflow.cpp):
//   C:\msys64\ucrt64\bin\g++.exe -std=c++17 -Wall -Wextra -DTASK7_UCRT_FILESYSTEM_ONLY -Iinclude -IEngine\Editor -IEngine\Serialization -IEngine\World -IEngine\Script -IEngine\Core -IEngine\Light -IEngine\Mesh -IEngine\Physics -IEngine\RayTracing Test\ScriptEditorWorkflowTest.cpp Engine\Editor\FEditorAssetWorkflow.cpp -o script_editor_filesystem_ucrt64_test.exe; if ($LASTEXITCODE -eq 0) { .\script_editor_filesystem_ucrt64_test.exe }
//
// Build/run from the repository root after building Engine.sln Debug|Win32:
//   $cmd = 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 >nul && cl /nologo /utf-8 /std:c++17 /EHsc /MDd /DWIN32 /D_DEBUG /Fo:bin\ScriptEditorWorkflowTest.obj /Iinclude /IEngine\Editor /IEngine\Serialization /IEngine\World /IEngine\Script /IEngine\Core /IEngine\Light /IEngine\Mesh /IEngine\Physics /IEngine\Import /IEngine\RayTracing /IEngine\Acceleration Test\ScriptEditorWorkflowTest.cpp /Fe:script_editor_workflow_test.exe bin\Engine.lib /link /OPT:NOREF,NOICF /LIBPATH:lib glew32.lib freeglut.lib glfw3dll.lib opengl32.lib glu32.lib assimp-vc143-mt.lib'; & cmd.exe /d /c $cmd
//   $env:Path = "$PWD\bin;$env:Path"; .\script_editor_workflow_test.exe

#include "FEditorAssetWorkflow.h"
#include "FWorldSerializer.h"
#include "UWorld.h"
#include "UScene.h"
#include "AActor.h"
#include "UScriptComponent.h"
#include "UMeshComponent.h"
#include "UBoxComponent.h"
#include "UScriptSubsystem.h"

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    int failures = 0;

    void Check(bool value, const char* name)
    {
        std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
        if (!value) ++failures;
    }

    void Write(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }

    std::string Read(const fs::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    }

    std::vector<std::string> AssetPaths(const std::vector<FEditorContentAsset>& assets)
    {
        std::vector<std::string> result;
        for (const auto& asset : assets)
            result.push_back(asset.relativePath.generic_string() + ":" + asset.category);
        return result;
    }

#ifndef TASK7_UCRT_FILESYSTEM_ONLY
    size_t Count(const std::string& text, const std::string& needle)
    {
        size_t count = 0;
        for (size_t pos = 0; (pos = text.find(needle, pos)) != std::string::npos; pos += needle.size())
            ++count;
        return count;
    }
#endif

    bool MakeDirectoryLink(const fs::path& link, const fs::path& target)
    {
        std::error_code ec;
        fs::create_directory_symlink(target, link, ec);
#ifdef _WIN32
        if (ec)
        {
            const std::wstring command = L"cmd.exe /d /c mklink /J \"" + link.wstring() +
                L"\" \"" + target.wstring() + L"\" >nul";
            return _wsystem(command.c_str()) == 0;
        }
#endif
        return !ec;
    }

    bool MakeFileLink(const fs::path& link, const fs::path& target)
    {
        std::error_code ec;
        fs::create_symlink(target, link, ec);
#ifdef _WIN32
        if (ec)
        {
            const std::wstring command = L"cmd.exe /d /c mklink \"" + link.wstring() +
                L"\" \"" + target.wstring() + L"\" >nul 2>nul";
            return _wsystem(command.c_str()) == 0;
        }
#endif
        return !ec;
    }

    struct TrackedComponent final : UActorComponent
    {
        explicit TrackedComponent(int& destructions) : destructions_(&destructions) {}
        ~TrackedComponent() override { ++*destructions_; }
        std::string_view TypeName() const override { return "Tracked"; }
        int* destructions_;
    };
}

int main()
{
    const fs::path root = fs::temp_directory_path() / ("Lua editor workflow " + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path content = root / "Content";
    Write(content / "Root.world", "world");
    Write(content / "Meshes" / "Nested.obj", "mesh");
    Write(content / "Scripts" / "Top.lua", "top");
    Write(content / "Scripts" / "Nested" / "Deep.LUA", "deep");
    Write(content / "Scripts" / "Nested" / "Ignore.txt", "ignore");
    const fs::path externalPayload = root / "External Files" / "Payload.txt";
    Write(externalPayload, "external payload");
    const bool madeFileEscape = MakeFileLink(content / "Scripts" / "Linked.lua", externalPayload);
    const fs::path externalTree = root / "External Tree";
    Write(externalTree / "Escaped.lua", "must stay external");
    const bool madeDiscoveryJunction = MakeDirectoryLink(content / "Scripts" / "LinkedOutside", externalTree);

    // Mutation caught: replacing recursive traversal with a root-only scan, flattening
    // relative paths, or failing to classify Lua case-insensitively.
    const auto assets = DiscoverEditorContent(content);
    const auto paths = AssetPaths(assets);
    Check(paths == std::vector<std::string>{
            "Meshes/Nested.obj:Mesh",
            "Root.world:World",
            "Scripts/Nested/Deep.LUA:Script",
            "Scripts/Nested/Ignore.txt:Other",
            "Scripts/Top.lua:Script" },
        "content discovery is recursive, deterministic, and preserves nested script paths");
    const auto nested = std::find_if(assets.begin(), assets.end(), [](const auto& asset) {
        return asset.relativePath.generic_string() == "Scripts/Nested/Deep.LUA";
    });
    Check(nested != assets.end() && ResolveEditorContentPath(content, *nested) == content / "Scripts/Nested/Deep.LUA",
        "content entries reconstruct the original full nested path");
    FEditorContentAsset forgedEscape{ fs::path("../External Files/Payload.txt"), "Script", "[Lua]" };
    FEditorContentAsset linkedEscape{ fs::path("Scripts/LinkedOutside/Escaped.lua"), "Script", "[Lua]" };
    Check(madeDiscoveryJunction &&
          std::none_of(assets.begin(), assets.end(), [](const auto& asset) {
              return asset.relativePath.generic_string().find("Payload.txt") != std::string::npos ||
                     asset.relativePath.generic_string().find("LinkedOutside") != std::string::npos;
          }) &&
          ResolveEditorContentPath(content, forgedEscape).empty() &&
          ResolveEditorContentPath(content, linkedEscape).empty() &&
          (!madeFileEscape || ResolveEditorContentPath(content,
              FEditorContentAsset{ fs::path("Scripts/Linked.lua"), "Script", "[Lua]" }).empty()) &&
          fs::exists(externalPayload),
        "content discovery and resolution reject lexical escapes and external reparses");

    std::string mutationError;
    Check(!RenameEditorContentAsset(content, linkedEscape, "Renamed.lua", mutationError) &&
          !DeleteEditorContentAsset(content, linkedEscape, mutationError) &&
          fs::exists(externalTree / "Escaped.lua"),
        "rename and delete reject content identities crossing an external reparse");
    Write(content / "Scripts" / "Mutable" / "Rename.lua", "rename me");
    Write(content / "Scripts" / "Mutable" / "Delete.lua", "delete me");
    FEditorContentAsset renameAsset{ fs::path("Scripts/Mutable/Rename.lua"), "Script", "[Lua]" };
    FEditorContentAsset deleteAsset{ fs::path("Scripts/Mutable/Delete.lua"), "Script", "[Lua]" };
    Check(RenameEditorContentAsset(content, renameAsset, "Renamed.lua", mutationError) &&
          DeleteEditorContentAsset(content, deleteAsset, mutationError) &&
          fs::exists(content / "Scripts/Mutable/Renamed.lua") &&
          !fs::exists(content / "Scripts/Mutable/Delete.lua"),
        "rename and delete preserve safe nested content identity");
    Write(content / "RootRename.lua", "root rename");
    FEditorContentAsset rootRenameAsset{ fs::path("RootRename.lua"), "Script", "[Lua]" };
    Check(RenameEditorContentAsset(content, rootRenameAsset, "RootRenamed.lua", mutationError) &&
          fs::exists(content / "RootRenamed.lua"),
        "rename preserves existing root-level content behavior");

    Write(content / "RootCollision.png", "root body");
    Write(content / "Textures" / "RootCollision.png", "nested body");
    bool copiedAsset = true;
    std::string copyError;
    const fs::path retainedNested = CopyEditorAssetToContent(
        content / "Textures/RootCollision.png", content, copiedAsset, copyError);
    Check(retainedNested == fs::absolute(content / "Textures/RootCollision.png").lexically_normal() &&
          !copiedAsset && copyError.empty() &&
          Read(content / "RootCollision.png") == "root body" &&
          Read(content / "Textures/RootCollision.png") == "nested body",
        "copy-to-content preserves an existing nested asset without flattening or overwrite");
    Check(EditorWorldNameFromPath(content, content / "Root.world") == fs::path("Root") &&
          EditorWorldNameFromPath(content, content / "Worlds/Nested.world") == fs::path("Worlds/Nested"),
        "root and nested world names preserve their content-relative save locations");
    const fs::path nestedWorldSave = PrepareEditorWorldSavePath(content, "Worlds/NewNested");
    const fs::path dottedWorldSave = PrepareEditorWorldSavePath(content, "Worlds/New.Version");
    Check(nestedWorldSave == content / "Worlds/NewNested.world" &&
          dottedWorldSave == content / "Worlds/New.Version.world" &&
          fs::is_directory(content / "Worlds"),
        "nested world save preparation creates its preserved parent path without changing dotted names");

    auto pickerModel = DiscoverEditorContent(content);
    const auto pickerIndex = static_cast<size_t>(std::distance(pickerModel.begin(),
        std::find_if(pickerModel.begin(), pickerModel.end(), [](const auto& asset) {
            return asset.relativePath.generic_string() == "Scripts/Top.lua";
        })));
    const fs::path stableSelection = SelectEditorScriptAsset(content, pickerModel, pickerIndex);
    pickerModel.clear();
    Check(stableSelection == fs::absolute(content / "Scripts/Top.lua").lexically_normal(),
        "script picker returns a stable path value before its model is refreshed");

#ifndef TASK7_UCRT_FILESYSTEM_ONLY

    AActor actor;
    actor.SetMesh(new UMeshComponent());
    auto& first = actor.AddComponent<UScriptComponent>();
    int destructions = 0;
    actor.AddComponent<TrackedComponent>(destructions);
    auto& second = actor.AddComponent<UScriptComponent>();
    actor.SetPhysics(new UBoxComponent());

    // Mutation caught: selecting the first component of a type instead of the exact
    // attachment identified by its stable actor component index.
    size_t firstIndex = 0, secondIndex = 0;
    for (size_t i = 0; i < actor.Components().size(); ++i)
    {
        if (actor.Components()[i].get() == &first) firstIndex = i;
        if (actor.Components()[i].get() == &second) secondIndex = i;
    }
    Check(firstIndex != secondIndex && ScriptComponentAt(actor, firstIndex) == &first &&
          ScriptComponentAt(actor, secondIndex) == &second && ScriptComponentAt(actor, firstIndex + 1) == nullptr,
        "script selection resolves the exact attachment index");

    int undoCount = 0;
    const fs::path external = root / "External Files" / "Assigned.lua";
    Write(external, "external body");
    const auto assigned = AssignEditorLuaScript(first, external, content, [&] { ++undoCount; });
    Check(assigned.succeeded && assigned.changed && assigned.validationMessage.empty() && undoCount == 1 &&
          first.ScriptPath() == fs::path("Content/Scripts/Assigned.lua") &&
          Read(content / "Scripts/Assigned.lua") == "external body",
        "external Lua assignment copies beneath Content Scripts before one undoable assignment");

    // Mutation caught: validating after changing the attachment, accepting missing,
    // directory, or non-Lua selections, or calling the undo hook for a rejected edit.
    const auto prior = first.ScriptPath();
    Write(root / "External Files" / "Wrong.txt", "not lua");
    fs::create_directories(root / "External Files" / "Folder.lua");
    const auto wrong = AssignEditorLuaScript(first, root / "External Files/Wrong.txt", content, [&] { ++undoCount; });
    const auto missing = AssignEditorLuaScript(first, root / "External Files/Missing.lua", content, [&] { ++undoCount; });
    const auto directory = AssignEditorLuaScript(first, root / "External Files/Folder.lua", content, [&] { ++undoCount; });
    Check(!wrong.succeeded && !wrong.validationMessage.empty() && !missing.succeeded &&
          !missing.validationMessage.empty() && !directory.succeeded && !directory.validationMessage.empty() &&
          first.ScriptPath() == prior && undoCount == 1,
        "invalid script choices report validation and leave the prior assignment atomic");

    UScriptComponent aliasComponent;
    const fs::path aliasPath = root / "External Files" / "Alias.lua";
    const bool madeAlias = MakeFileLink(aliasPath, externalPayload);
    const fs::path aliasedDirectoryTarget = root / "Aliased Source";
    Write(aliasedDirectoryTarget / "Alias.lua", "aliased Lua body");
    const bool madeAliasJunction = MakeDirectoryLink(root / "External Files" / "LinkedAlias", aliasedDirectoryTarget);
    const auto aliasResult = madeAlias
        ? AssignEditorLuaScript(aliasComponent, aliasPath, content, {})
        : AssignEditorLuaScript(aliasComponent,
            root / "External Files/LinkedAlias/Alias.lua", content, {});
    Check((madeAlias || madeAliasJunction) && !aliasResult.succeeded && !aliasResult.validationMessage.empty() &&
          aliasComponent.ScriptPath().empty() && !fs::exists(content / "Scripts/Payload.txt") &&
          !fs::exists(content / "Scripts/Alias.lua"),
        "assignment rejects Lua selections containing a file or directory reparse");

    const fs::path rollbackSource = root / "External Files" / "Rollback.lua";
    Write(rollbackSource, "rollback body");
    UScriptComponent rollbackComponent;
    const auto rolledBack = AssignEditorLuaScript(rollbackComponent, rollbackSource, content, [] {
        throw std::runtime_error("undo callback rejected change");
    });
    Check(!rolledBack.succeeded && !rolledBack.validationMessage.empty() &&
          rollbackComponent.ScriptPath().empty() && !fs::exists(content / "Scripts/Rollback.lua"),
        "failed component assignment rolls back an external Lua copy");

    const fs::path escapedContent = root / "Escaped Project" / "Content";
    const fs::path escapedTarget = root / "Outside Project";
    fs::create_directories(escapedContent);
    fs::create_directories(escapedTarget);
    const bool madeEscape = MakeDirectoryLink(escapedContent / "Scripts", escapedTarget);
    const fs::path escapeSource = root / "External Files" / "ReparseEscape.lua";
    Write(escapeSource, "must remain external");
    UScriptComponent escapeComponent;
    const auto escaped = madeEscape
        ? AssignEditorLuaScript(escapeComponent, escapeSource, escapedContent, {})
        : FEditorScriptAssignment{};
    Check(madeEscape && !escaped.succeeded && !escaped.validationMessage.empty() &&
          escapeComponent.ScriptPath().empty() && !fs::exists(escapedTarget / escapeSource.filename()),
        "assignment rejects a reparse-pointed Scripts directory before copying outside Content");

    const fs::path linkedProject = root / "Linked Content Project";
    const fs::path linkedContentTarget = root / "Outside Linked Content";
    fs::create_directories(linkedProject);
    fs::create_directories(linkedContentTarget);
    const bool madeContentEscape = MakeDirectoryLink(linkedProject / "Content", linkedContentTarget);
    UScriptComponent contentEscapeComponent;
    const auto contentEscaped = madeContentEscape
        ? AssignEditorLuaScript(contentEscapeComponent, escapeSource, linkedProject / "Content", {})
        : FEditorScriptAssignment{};
    Check(madeContentEscape && !contentEscaped.succeeded && !contentEscaped.validationMessage.empty() &&
          contentEscapeComponent.ScriptPath().empty() && !fs::exists(linkedContentTarget / "Scripts"),
        "assignment validates a reparse-pointed Content root before creating Scripts outside the project");

    const auto nestedAssigned = AssignEditorLuaScript(second, content / "Scripts/Nested/Deep.LUA", content,
        [&] { ++undoCount; });
    Check(nestedAssigned.succeeded && second.ScriptPath() == fs::path("Content/Scripts/Nested/Deep.LUA") &&
          Read(content / "Scripts/Nested/Deep.LUA") == "deep" && undoCount == 2,
        "existing nested Lua assignment preserves its project-relative subpath without copying");

    second.SetEnabled(false);
    second.ClearScriptPath();
    UWorld world;
    auto* savedActor = new AActor();
    savedActor->name = "Scripts";
    savedActor->AddComponent<UScriptComponent>(first);
    savedActor->AddComponent<UScriptComponent>(second);
    world.Spawn(savedActor);
    const std::string saved = FWorldSerializer::Save(world);
    std::unique_ptr<UWorld> loaded(FWorldSerializer::Load(saved));
    const std::string resaved = loaded ? FWorldSerializer::Save(*loaded) : "";
    AActor* loadedActor = loaded && !loaded->GetScene().Actors.empty() ? loaded->GetScene().Actors.front() : nullptr;
    auto* loadedUnassigned = loadedActor ? ScriptComponentAt(*loadedActor, 1) : nullptr;
    Check(Count(saved, "Type = ScriptComponent") == 2 && Count(saved, "Script =") == 1 &&
          loadedUnassigned && loadedUnassigned->ScriptPath().empty() && !loadedUnassigned->IsEnabled() && saved == resaved,
        "clear keeps an unassigned component and omitted Script serialization is stable");

    // Mutation caught: generic erasure of aliased components, removal of a foreign
    // pointer, erasing the wrong duplicate type, or reordering survivors.
    const auto* mesh = actor.mesh;
    const auto* physics = actor.physics;
    AActor foreign;
    auto& foreignScript = foreign.AddComponent<UScriptComponent>();
    auto orphan = std::make_unique<UScriptComponent>();
    const std::vector<UActorComponent*> before = [&] {
        std::vector<UActorComponent*> value;
        for (const auto& component : actor.Components()) value.push_back(component.get());
        return value;
    }();
    Check(!actor.RemoveNonSpatialComponent(nullptr) &&
          !actor.RemoveNonSpatialComponent(&foreignScript) &&
          !actor.RemoveNonSpatialComponent(orphan.get()) &&
          !actor.RemoveNonSpatialComponent(actor.mesh) &&
          !actor.RemoveNonSpatialComponent(actor.physics) && actor.mesh == mesh && actor.physics == physics,
        "non-spatial removal rejects null foreign and specialized alias components");
    Check(actor.RemoveNonSpatialComponent(&second) &&
          actor.Components().size() == before.size() - 1 &&
          actor.Components()[0].get() == before[0] && actor.Components()[1].get() == before[1] &&
          actor.Components()[2].get() == before[2] && actor.Components()[3].get() == before[4],
        "removal destroys exactly the requested script and preserves every survivor order");
    auto* tracked = actor.Components()[2].get();
    Check(actor.RemoveNonSpatialComponent(tracked) && destructions == 1,
        "non-spatial removal destroys the owned requested component exactly once");

    // Mutation caught: editor-side assignment, scanning, saving, or removal starting
    // a Lua instance outside world BeginPlay.
    std::vector<std::string> logs;
    UScriptSubsystem scripts([&](std::string_view text) { logs.emplace_back(text); });
    scripts.SetProjectRoot(root);
    scripts.Init();
    UWorld editWorld;
    editWorld.SetScriptSubsystem(&scripts);
    auto* editing = new AActor();
    auto& editingScript = editing->AddComponent<UScriptComponent>();
    editWorld.Spawn(editing);
    AssignEditorLuaScript(editingScript, content / "Scripts/Top.lua", content, {});
    FWorldSerializer::Save(editWorld);
    editWorld.Tick(0.25f);
    editing->RemoveNonSpatialComponent(&editingScript);
    Check(logs.empty(), "all editor assignment operations remain Lua-free before Play");
    scripts.Shutdown();
#endif

    fs::remove_all(root);
    std::cout << "=== script editor workflow: " << failures << " failed ===\n";
    return failures == 0 ? 0 : 1;
}
