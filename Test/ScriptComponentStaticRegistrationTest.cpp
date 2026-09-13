// ---------------------------------------------------------------------------
// ScriptComponentStaticRegistrationTest.cpp -- verifies a static-library world
// loader reaches ScriptComponent registration without naming the concrete type.
//
// Build (MSVC Debug|Win32; run from repository root):
//   $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
//   $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
//   & $msbuild Engine.sln /m /p:Configuration=Debug /p:Platform=Win32 /clp:ErrorsOnly
//   $cmd = 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 >nul && cl /nologo /std:c++17 /EHsc /MDd /DWIN32 /D_DEBUG /Iinclude /IEngine\Serialization /IEngine\World /IEngine\Script /IEngine\Core /IEngine\Light /IEngine\Mesh /IEngine\Physics /IEngine\Import /IEngine\RayTracing /IEngine\Acceleration Test\ScriptComponentStaticRegistrationTest.cpp /Fe:script_component_static_registration_test.exe bin\Engine.lib /link /LIBPATH:lib glew32.lib freeglut.lib glfw3dll.lib opengl32.lib glu32.lib assimp-vc143-mt.lib'; & cmd.exe /d /c $cmd
//   $env:Path = "$PWD\bin;$env:Path"; .\script_component_static_registration_test.exe
// ---------------------------------------------------------------------------
#include "FWorldSerializer.h"
#include "UWorld.h"
#include "UScene.h"
#include "AActor.h"

#include <cstdio>
#include <string>

int main()
{
    // This translation unit deliberately never includes or constructs
    // UScriptComponent. A static Engine.lib must still load the script block.
    UWorld* world = FWorldSerializer::Load(
        "WorldFormat = 2\n\n[Actor]\nType = Actor\nName = StaticConsumer\n\n"
        "[Component]\nType = ScriptComponent\nEnabled = 1\nScript = Content/Scripts/Static.lua\n");
    AActor* actor = world && !world->GetScene().Actors.empty()
        ? world->GetScene().Actors.front() : nullptr;
    const bool ok = actor && actor->Components().size() == 1 &&
        std::string(actor->Components().front()->TypeName()) == "ScriptComponent";
    std::printf("static script component registration: %s\n", ok ? "PASS" : "FAIL");
    delete world;
    return ok ? 0 : 1;
}
