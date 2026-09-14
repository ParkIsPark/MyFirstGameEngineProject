// Current complete recipe: powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name ScriptComponentFactoryAutoRegistrationTest
// Prerequisite: Engine.sln Debug|Win32. Older direct compile examples follow.
// ---------------------------------------------------------------------------
// ScriptComponentFactoryAutoRegistrationTest.cpp -- a static-library consumer
// links the concrete type and queries its factory registration before any world
// load occurs.
//
// Build (MSVC Debug|Win32; run from repository root):
//   $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
//   $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
//   & $msbuild Engine.sln /m /p:Configuration=Debug /p:Platform=Win32 /clp:ErrorsOnly
//   $cmd = 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 >nul && cl /nologo /std:c++17 /EHsc /MDd /DWIN32 /D_DEBUG /Iinclude /IEngine\Serialization /IEngine\World /IEngine\Script /IEngine\Core /IEngine\Light /IEngine\Mesh /IEngine\Physics /IEngine\Import /IEngine\RayTracing /IEngine\Acceleration Test\ScriptComponentFactoryAutoRegistrationTest.cpp /Fe:script_component_factory_auto_registration_test.exe bin\Engine.lib /link /LIBPATH:lib glew32.lib freeglut.lib glfw3dll.lib opengl32.lib glu32.lib assimp-vc143-mt.lib'; & cmd.exe /d /c $cmd
//   $env:Path = "$PWD\bin;$env:Path"; .\script_component_factory_auto_registration_test.exe
// ---------------------------------------------------------------------------
#include "FArchive.h"
#include "UScriptComponent.h"

#include <cstdio>
#include <string>

int main()
{
    // Calling this out-of-line member pulls UScriptComponent.cpp from Engine.lib
    // before the factory lookup, without loading a world.
    UScriptComponent linkedConcreteType;
    linkedConcreteType.SetScriptPath("Content/Scripts/Linked.lua");

    UActorComponent* created = FComponentFactory::Create("ScriptComponent");
    const bool ok = created && std::string(created->TypeName()) == "ScriptComponent";
    std::printf("script component factory auto-registration: %s\n", ok ? "PASS" : "FAIL");
    delete created;
    return ok ? 0 : 1;
}
