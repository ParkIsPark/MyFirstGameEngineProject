// ---------------------------------------------------------------------------
// ScriptComponentSerializationTest.cpp -- focused GL-free Task 5 coverage.
//
// Build (MSVC Debug|Win32; run from repository root):
//   $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
//   $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
//   & $msbuild Engine.sln /m /p:Configuration=Debug /p:Platform=Win32 /clp:ErrorsOnly
//   $cmd = 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 >nul && cl /nologo /std:c++17 /EHsc /MDd /DWIN32 /D_DEBUG /Iinclude /IEngine\Serialization /IEngine\World /IEngine\Script /IEngine\Core /IEngine\Light /IEngine\Mesh /IEngine\Physics /IEngine\Import /IEngine\RayTracing /IEngine\Acceleration Test\ScriptComponentSerializationTest.cpp /Fe:script_component_serialization_test.exe bin\Engine.lib /link /LIBPATH:lib glew32.lib freeglut.lib glfw3dll.lib opengl32.lib glu32.lib assimp-vc143-mt.lib'; & cmd.exe /d /c $cmd
//   $env:Path = "$PWD\bin;$env:Path"; .\script_component_serialization_test.exe
// ---------------------------------------------------------------------------
#include "FArchive.h"
#include "FWorldSerializer.h"
#include "UWorld.h"
#include "UScene.h"
#include "AActor.h"
#include "USceneComponent.h"
#include "UScriptComponent.h"
#include "UMeshComponent.h"
#include "UBoxComponent.h"
#include "ALight.h"
#include "PointLightComponent.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    int passed = 0;
    int failed = 0;

    void Check(const char* name, bool result)
    {
        std::printf("[%s] %s\n", result ? "PASS" : "FAIL", name);
        std::fflush(stdout);
        result ? ++passed : ++failed;
    }

    template<class F>
    bool Throws(F&& action)
    {
        try { action(); }
        catch (const std::invalid_argument&) { return true; }
        return false;
    }

    std::vector<UScriptComponent*> ScriptComponents(const AActor& actor)
    {
        std::vector<UScriptComponent*> result;
        for (const auto& component : actor.Components())
            if (auto* script = dynamic_cast<UScriptComponent*>(component.get())) result.push_back(script);
        return result;
    }

    std::string ReadFixture()
    {
        std::ifstream input("Test/Fixtures/ScriptComponentWorld.world");
        return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    }

    size_t CountOccurrences(const std::string& text, const std::string& needle)
    {
        size_t count = 0;
        for (size_t pos = 0; (pos = text.find(needle, pos)) != std::string::npos; pos += needle.size())
            ++count;
        return count;
    }
}

int main()
{
    // Mutation caught: accepting an invalid spelling or mutating before reject.
    {
        UScriptComponent component;
        component.SetScriptPath("Content\\Scripts/./Rotate.lua");
        const auto original = component.ScriptPath();
        const bool bad =
            Throws([&] { component.SetScriptPath(""); }) &&
            Throws([&] { component.SetScriptPath("/Content/Scripts/Rotate.lua"); }) &&
            Throws([&] { component.SetScriptPath("C:/Content/Scripts/Rotate.lua"); }) &&
            Throws([&] { component.SetScriptPath("Content/Scripts/../Escape.lua"); }) &&
            Throws([&] { component.SetScriptPath("Content/Other/Rotate.lua"); }) &&
            Throws([&] { component.SetScriptPath("Content/Scripts"); }) &&
            Throws([&] { component.SetScriptPath("Content/Scripts/"); }) &&
            Throws([&] { component.SetScriptPath("Content/Scripts/."); }) &&
            Throws([&] { component.SetScriptPath("Content/Scripts/Rotate:alt.lua"); });
        Check("script path normalizes valid spelling and rejects invalid input without mutation",
            component.ScriptPath() == std::filesystem::path("Content/Scripts/Rotate.lua") &&
            component.ScriptPath() == original && bad);
    }

    // Mutation caught: treating an explicit empty Script as an omitted legacy field.
    {
        UScriptComponent component;
        component.SetEnabled(true);
        component.SetScriptPath("Content/Scripts/Keep.lua");
        FLoadArchive explicitEmpty("Enabled = 0\nScript =\n");
        const bool rejected = Throws([&] { component.Serialize(explicitEmpty); });
        Check("explicit empty serialized script is rejected without changing existing state",
            rejected && component.IsEnabled() &&
            component.ScriptPath() == std::filesystem::path("Content/Scripts/Keep.lua"));
    }

    // Mutation caught: using a representable control byte as the missing-field marker.
    {
        UScriptComponent component;
        component.SetEnabled(true);
        component.SetScriptPath("Content/Scripts/Keep.lua");
        FLoadArchive explicitControl("Enabled = 0\nScript = \x1D\n");
        const bool rejected = Throws([&] { component.Serialize(explicitControl); });
        Check("explicit control-character script is rejected without changing existing state",
            rejected && component.IsEnabled() &&
            component.ScriptPath() == std::filesystem::path("Content/Scripts/Keep.lua"));
    }

    // Mutation caught: omitting the base Enabled field or Script field.
    {
        UScriptComponent source;
        source.SetEnabled(false);
        source.SetScriptPath("Content/Scripts/Rotate.lua");
        FSaveArchive saved;
        source.Serialize(saved);
        UScriptComponent loaded;
        FLoadArchive archive(saved.str());
        loaded.Serialize(archive);
        Check("direct script component archive round trip preserves normalized path and enabled state",
            !loaded.IsEnabled() && loaded.ScriptPath() == std::filesystem::path("Content/Scripts/Rotate.lua"));
    }

    // Mutation caught: applying Enabled before an invalid serialized Script is rejected.
    {
        UScriptComponent component;
        component.SetEnabled(true);
        component.SetScriptPath("Content/Scripts/Keep.lua");
        FLoadArchive invalid("Enabled = 0\nScript = ../escape.lua\n");
        const bool rejected = Throws([&] { component.Serialize(invalid); });
        Check("invalid serialized script leaves all prior component state intact",
            rejected && component.IsEnabled() &&
            component.ScriptPath() == std::filesystem::path("Content/Scripts/Keep.lua"));
    }

    // Mutation caught: serializing only scene children, or reordering mixed components.
    {
        UWorld world;
        AActor* actor = new AActor();
        actor->name = "ScriptedCube";
        auto& first = actor->AddComponent<UScriptComponent>();
        first.SetScriptPath("Content/Scripts/First.lua");
        first.SetEnabled(false);
        auto& mesh = actor->AddComponent<UMeshComponent>();
        mesh.name = "Spatial";
        mesh.AttachTo(&actor->rootComponent);
        auto& second = actor->AddComponent<UScriptComponent>();
        second.SetScriptPath("Content/Scripts/Second.lua");
        world.Spawn(actor);

        const std::string saved = FWorldSerializer::Save(world);
        UWorld* loadedWorld = FWorldSerializer::Load(saved);
        AActor* loaded = loadedWorld && !loadedWorld->GetScene().Actors.empty()
            ? loadedWorld->GetScene().Actors.front() : nullptr;
        const auto scripts = loaded ? ScriptComponents(*loaded) : std::vector<UScriptComponent*>{};
        Check("format 2 world preserves script component order, state, and spatial attachment",
            saved.find("WorldFormat = 2") != std::string::npos && loaded &&
            loaded->Components().size() == 3 && scripts.size() == 2 &&
            scripts[0]->ScriptPath() == std::filesystem::path("Content/Scripts/First.lua") && !scripts[0]->IsEnabled() &&
            scripts[1]->ScriptPath() == std::filesystem::path("Content/Scripts/Second.lua") && scripts[1]->IsEnabled() &&
            dynamic_cast<UMeshComponent*>(loaded->Components()[1].get()) &&
            dynamic_cast<UMeshComponent*>(loaded->Components()[1].get())->attachParent == &loaded->rootComponent);
        delete loadedWorld;
    }

    // Mutation caught: treating all factory output as a USceneComponent.
    {
        UActorComponent* script = FComponentFactory::Create("ScriptComponent");
        UActorComponent* scene = FComponentFactory::Create("Scene");
        Check("generic component factory creates script and scene components with checked scene cast",
            script && std::string(script->TypeName()) == "ScriptComponent" && !dynamic_cast<USceneComponent*>(script) &&
            scene && dynamic_cast<USceneComponent*>(scene));
        delete script;
        delete scene;
    }

    // Mutation caught: duplicating legacy physics as [Component], losing
    // typed aliases, or dropping script attachments from a mixed world.
    {
        UWorld world;
        AActor* actor = new AActor();
        actor->name = "Mixed";
        auto* mesh = new UMeshComponent();
        mesh->meshRef = "Cube 1 1 1";
        actor->SetMesh(mesh);
        actor->SetPhysics(new UBoxComponent());
        actor->AddComponent<UScriptComponent>().SetScriptPath("Content/Scripts/Mixed.lua");
        ALight* light = new ALight(new PointLightComponent());
        light->AddComponent<UScriptComponent>().SetScriptPath("Content/Scripts/Light.lua");
        world.Spawn(actor);
        world.Spawn(light);

        const std::string saved = FWorldSerializer::Save(world);
        UWorld* loaded = FWorldSerializer::Load(saved);
        AActor* loadedActor = loaded && loaded->GetScene().Actors.size() > 0
            ? loaded->GetScene().Actors[0] : nullptr;
        ALight* loadedLight = loaded && loaded->GetScene().Actors.size() > 1
            ? dynamic_cast<ALight*>(loaded->GetScene().Actors[1]) : nullptr;
        Check("mixed mesh light physics and script world preserves aliases without physics duplication",
            loadedActor && loadedActor->mesh && loadedActor->physics &&
            ScriptComponents(*loadedActor).size() == 1 && loadedLight && loadedLight->lightComp &&
            ScriptComponents(*loadedLight).size() == 1 &&
            CountOccurrences(saved, "[Collision]") == 1 &&
            saved.find("Type = Primitive") == std::string::npos);
        delete loaded;
    }

    // Mutation caught: discarding an actor after an unknown component instead of skipping one block.
    {
        std::vector<std::string> warnings;
        FWorldSerializer::SetWarningSink([&](std::string_view message) { warnings.emplace_back(message); });
        UWorld* loaded = FWorldSerializer::Load(
            "WorldFormat = 2\n\n[Actor]\nType = Actor\nName = Keep\n\n[Component]\nType = UnknownThing\n\n[Component]\nType = ScriptComponent\nEnabled = 0\nScript = Content/Scripts/Later.lua\n");
        FWorldSerializer::SetWarningSink({});
        AActor* actor = loaded && !loaded->GetScene().Actors.empty() ? loaded->GetScene().Actors.front() : nullptr;
        const auto scripts = actor ? ScriptComponents(*actor) : std::vector<UScriptComponent*>{};
        Check("unknown component emits useful warning and later script component still loads",
            actor && actor->name == "Keep" && scripts.size() == 1 && !scripts[0]->IsEnabled() &&
            scripts[0]->ScriptPath() == std::filesystem::path("Content/Scripts/Later.lua") &&
            warnings.size() == 1 && warnings[0].find("UnknownThing") != std::string::npos);
        delete loaded;
    }

    // Mutation caught: dropping format-1 component defaults or making a format-2 fixture unstable.
    {
        UWorld* old = FWorldSerializer::Load(
            "WorldFormat = 1\n\n[Actor]\nType = Actor\nName = Legacy\nLoc = 1 2 3\n\n"
            "[Collision]\nShape = Box\n");
        const std::string fixture = ReadFixture();
        UWorld* fixtureLoaded = FWorldSerializer::Load(fixture);
        const std::string resaved = fixtureLoaded ? FWorldSerializer::Save(*fixtureLoaded) : "";
        Check("format 1 preserves existing component defaults and format 2 fixture is save-load-save stable",
            old && old->GetScene().Actors.size() == 1 && old->GetScene().Actors.front()->name == "Legacy" &&
            old->GetScene().Actors.front()->physics && old->GetScene().Actors.front()->physics->IsEnabled() &&
            fixtureLoaded && fixture == resaved);
        delete old;
        delete fixtureLoaded;
    }

    std::printf("=== script component serialization: %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
