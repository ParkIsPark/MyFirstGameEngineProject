// ---------------------------------------------------------------------------
// ComponentHandoffExceptionSafetyTest.cpp -- deterministic SetMesh/SetLight
// ownership test when root attachment allocation fails after adoption would
// otherwise have happened.
//
// Build (MSVC Debug|Win32; run from repository root):
//   $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
//   $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
//   & $msbuild Engine.sln /m /p:Configuration=Debug /p:Platform=Win32 /clp:ErrorsOnly
//   $cmd = 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 >nul && cl /nologo /std:c++17 /EHsc /MDd /DWIN32 /D_DEBUG /Iinclude /IEngine\Serialization /IEngine\World /IEngine\Script /IEngine\Core /IEngine\Light /IEngine\Mesh /IEngine\Physics /IEngine\Import /IEngine\RayTracing /IEngine\Acceleration Test\ComponentHandoffExceptionSafetyTest.cpp /Fe:component_handoff_exception_test.exe bin\Engine.lib /link /LIBPATH:lib glew32.lib freeglut.lib glfw3dll.lib opengl32.lib glu32.lib assimp-vc143-mt.lib'; & cmd.exe /d /c $cmd
//   $env:Path = "$PWD\bin;$env:Path"; .\component_handoff_exception_test.exe
// ---------------------------------------------------------------------------
#include "AActor.h"
#include "ALight.h"
#include "UMeshComponent.h"
#include "UBoxComponent.h"
#include "LightComponent.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>

namespace
{
    bool failNextAllocation = false;

    template<class Actor, class Component>
    bool LeavesIncomingOutsideActorOnAttachmentAllocationFailure(Actor& actor,
        std::unique_ptr<Component>& incoming)
    {
        // Reserve actor component capacity without placing any scene child.
        actor.SetPhysics(new UBoxComponent());
        actor.SetPhysics(nullptr);
        assert(actor.Components().empty() && actor.rootComponent.children.empty());

        failNextAllocation = true;
        bool threw = false;
        try
        {
            if constexpr (std::is_same_v<Component, UMeshComponent>) actor.SetMesh(incoming.get());
            else actor.SetLightComponent(incoming.get());
        }
        catch (const std::bad_alloc&) { threw = true; }

        const bool safe = threw && incoming->GetOwner() == nullptr &&
            actor.Components().empty() && actor.rootComponent.children.empty();
        // Preserve process safety while running against the known-broken code:
        // it has already transferred ownership before the throwing attachment.
        if (incoming->GetOwner() == &actor) incoming.release();
        return safe;
    }
}

void* operator new(std::size_t size)
{
    if (failNextAllocation)
    {
        failNextAllocation = false;
        throw std::bad_alloc();
    }
    if (void* memory = std::malloc(size)) return memory;
    throw std::bad_alloc();
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

int main()
{
    AActor meshActor;
    auto mesh = std::make_unique<UMeshComponent>();
    const bool meshSafe = LeavesIncomingOutsideActorOnAttachmentAllocationFailure(meshActor, mesh);

    ALight lightActor;
    auto light = std::make_unique<LightComponent>();
    const bool lightSafe = LeavesIncomingOutsideActorOnAttachmentAllocationFailure(lightActor, light);

    std::printf("component handoff exception safety: %s\n", meshSafe && lightSafe ? "PASS" : "FAIL");
    return meshSafe && lightSafe ? 0 : 1;
}
