// GL-free real Engine/Editor boot and clone boundary test. From repo root:
// $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
// $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
// & $msbuild Engine.sln /m /p:Configuration=Debug /p:Platform=Win32 /clp:ErrorsOnly
// $includes = (Get-ChildItem Engine -Directory -Recurse).FullName | ForEach-Object { '/I"' + $_ + '"' }
// $cmd = 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 >nul && cl /nologo /std:c++17 /EHsc /MDd /DWIN32 /D_DEBUG /Iinclude ' + ($includes -join ' ') + ' Test\ScriptEditorBootTest.cpp /Fe:script_editor_boot_test.exe bin\Engine.lib /link /LIBPATH:lib glew32.lib freeglut.lib glfw3dll.lib opengl32.lib glu32.lib assimp-vc143-mt.lib'; & cmd.exe /d /c $cmd
// $env:Path = "$PWD\bin;$env:Path"; .\script_editor_boot_test.exe
#include "Engine.h"
#include "EditorEngine.h"
#include "UScriptSubsystem.h"
#include "UScriptComponent.h"
#include "FWorldSerializer.h"
#include "ALight.h"
#include "PointLightComponent.h"
#include "UMeshComponent.h"
#include "UBoxComponent.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <sstream>

template<class T> struct BootAdapter : T {
    using T::BootWorld;
    UWorld* StartupWorld() { return this->World(); }
    void StartupName(const char* name) { this->proj_.startupWorld = name; }
    UScriptSubsystem* Scripts() { return this->subsystems_.template Get<UScriptSubsystem>(); }
};
struct EditorAdapter : BootAdapter<EditorEngine> {
    using EditorEngine::CloneActor;
    using EditorEngine::CopyWorld;
};
int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("Lua editor boot " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "Content/Scripts");
    std::ofstream(root / "Content/Scripts/Boot.lua") <<
        "local count = 0\n"
        "function BeginPlay() Engine.Log('boot begin:' .. count) end\n"
        "function Tick() count = count + 1; Engine.Log('boot tick:' .. count) end\n"
        "function EndPlay() Engine.Log('boot end:' .. count) end\n";
    UWorld source; auto* actor = new ALight(); actor->name = "Ordered clone";
    actor->AddComponent<UScriptComponent>().SetScriptPath("Content/Scripts/Boot.lua");
    actor->SetMesh(new UMeshComponent());
    auto& second = actor->AddComponent<UScriptComponent>(); second.SetScriptPath("Content/Scripts/Boot.lua"); second.SetEnabled(false);
    actor->SetLightComponent(new PointLightComponent({.2f,.3f,.4f}, glm::vec3(5)));
    auto* box = new UBoxComponent(); box->velocity = {1,2,3}; actor->SetPhysics(box); source.Spawn(actor);
    FWorldSerializer::SaveToFile(source, (root / "Content/Startup.world").string().c_str());
    int failed = 0;
    auto check = [&](bool value, const char* name) { std::cout << (value ? "PASS " : "FAIL ") << name << std::endl; if (!value) ++failed; };
    {
        EditorAdapter editor; editor.StartupName("Startup");
        std::ostringstream output; auto* prior = std::cout.rdbuf(output.rdbuf());
        editor.BootWorld(root.string()); std::cout.rdbuf(prior);
        check(!editor.StartupWorld() && output.str().find("boot begin") == std::string::npos, "actual editor boot never loads or begins hidden game world");
        bool cacheAvailable = true;
        try { editor.Scripts()->ClearScriptCache(); } catch (const std::logic_error&) { cacheAvailable = false; }
        check(cacheAvailable, "editor boot leaves no live instance blocking PIE cache reset");
        if (editor.StartupWorld()) editor.StartupWorld()->EndPlay();
        std::unique_ptr<UWorld> copy(editor.CopyWorld(source, true));
        auto* cloned = dynamic_cast<ALight*>(copy->GetScene().Actors[0]);
        bool ordered = cloned && cloned->Components().size() == actor->Components().size();
        for (size_t i = 0; ordered && i < actor->Components().size(); ++i) ordered = actor->Components()[i]->TypeName() == cloned->Components()[i]->TypeName();
        check(ordered && cloned->mesh && cloned->lightComp && cloned->physics && cloned->physics->velocity == glm::vec3(0), "production CopyWorld preserves mixed component order typed aliases and resets physics");
        check(ordered && dynamic_cast<UScriptComponent*>(cloned->Components()[2].get())->ScriptPath() == second.ScriptPath() && !cloned->Components()[2]->IsEnabled(), "multiple script clone configuration stays at original indices");
        const std::string authoredBeforePIE = FWorldSerializer::Save(source);
        for (int cycle = 0; cycle != 2; ++cycle)
        {
            std::unique_ptr<UWorld> pie(editor.CopyWorld(source, true));
            std::ostringstream lifecycle;
            prior = std::cout.rdbuf(lifecycle.rdbuf());
            pie->SetScriptSubsystem(editor.Scripts());
            pie->BeginPlay();
            pie->Tick(0.25f);
            pie->EndPlay();
            std::cout.rdbuf(prior);
            const std::string callbacks = lifecycle.str();
            check(callbacks.find("boot begin:0") != std::string::npos &&
                  callbacks.find("boot tick:1") != std::string::npos &&
                  callbacks.find("boot end:1") != std::string::npos,
                cycle == 0 ? "first production PIE helper cycle runs fresh lifecycle" :
                             "second production PIE helper cycle runs fresh lifecycle");
            bool released = true;
            try { editor.Scripts()->ClearScriptCache(); }
            catch (const std::logic_error&) { released = false; }
            check(released && FWorldSerializer::Save(source) == authoredBeforePIE,
                cycle == 0 ? "first PIE Stop releases runtime and preserves edit world" :
                             "second PIE Stop releases runtime and preserves edit world");
        }
    }
    {
        BootAdapter<Engine> game; game.StartupName("Startup");
        std::ostringstream output; auto* prior = std::cout.rdbuf(output.rdbuf());
        game.BootWorld(root.string()); std::cout.rdbuf(prior);
        check(game.StartupWorld() && output.str().find("boot begin") != std::string::npos, "same boot path starts standalone world relative to supplied project root");
        if (game.StartupWorld()) game.StartupWorld()->EndPlay();
    }
    fs::remove_all(root);
    return failed ? 1 : 0;
}
