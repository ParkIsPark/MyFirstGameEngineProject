// Current complete recipe: powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name RenderSettingsMigrationTest
// Prerequisite: Engine.sln Debug|Win32. Older direct compile examples follow.
// ---------------------------------------------------------------------------
// RenderSettingsMigrationTest.cpp -- Task 3 named renderer settings migration.
//
// Build/run from a VS developer environment after Engine.sln Debug|Win32:
//   cl /nologo /std:c++17 /EHsc /MDd /DWIN32 /D_DEBUG /Iinclude /IEngine\Serialization /IEngine\Framework /IEngine\World /IEngine\Script /IEngine\Core /IEngine\Light /IEngine\Mesh /IEngine\Physics /IEngine\Import /IEngine\RayTracing /IEngine\Acceleration /IEngine\Render Test\RenderSettingsMigrationTest.cpp /Fe:render_settings_migration_test.exe bin\Engine.lib /link /LIBPATH:lib glew32.lib freeglut.lib glfw3dll.lib opengl32.lib glu32.lib assimp-vc143-mt.lib
//   set PATH=%CD%\bin;%PATH% && render_settings_migration_test.exe
// ---------------------------------------------------------------------------
#include "FProjectDescriptor.h"
#include "FWorldSerializer.h"
#include "UWorld.h"
#include "UScene.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    int passed = 0;
    int failed = 0;

    void Check(const char* name, bool result)
    {
        std::printf("[%s] %s\n", result ? "PASS" : "FAIL", name);
        result ? ++passed : ++failed;
    }

    std::string Read(const char* path)
    {
        std::ifstream file(path);
        return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
    }

    void Write(const char* path, const char* contents)
    {
        std::ofstream file(path);
        file << contents;
    }

    bool IsLegacyAllOn(const FRenderFeatures& f)
    {
        return f.hardwareRaster && f.rayTracing && f.rayTracedShadows &&
               f.rayTracedGI && f.rayTracedReflections &&
               f.rayTracingBackend == ERayTracingBackend::Auto;
    }
}

int main()
{
    // Mutation caught: mapping any legacy mode to the wrong named RT state or
    // reporting more/less than one migration diagnostic per loaded world.
    for (int mode = 0; mode != 3; ++mode)
    {
        const std::string path = "Test/Fixtures/WorldFormat1Mode" + std::to_string(mode) + ".world";
        std::vector<std::string> warnings;
        FWorldSerializer::SetWarningSink([&](std::string_view message) { warnings.emplace_back(message); });
        std::unique_ptr<UWorld> world(FWorldSerializer::Load(Read(path.c_str())));
        FWorldSerializer::SetWarningSink({});
        const bool expected = world && (mode == 0
            ? world->GetScene().renderFeatures.hardwareRaster && !world->GetScene().renderFeatures.rayTracing
            : IsLegacyAllOn(world->GetScene().renderFeatures));
        Check(("world format 1 mode " + std::to_string(mode) + " migrates exactly").c_str(),
              expected && warnings.size() == 1 && warnings[0].find("RenderMode") != std::string::npos);
    }

    // Mutation caught: format 2 no longer taking the same legacy path.
    {
        std::vector<std::string> warnings;
        FWorldSerializer::SetWarningSink([&](std::string_view message) { warnings.emplace_back(message); });
        std::unique_ptr<UWorld> world(FWorldSerializer::Load(
            "WorldFormat = 2\n\n[World]\nRenderMode = 1\n"));
        FWorldSerializer::SetWarningSink({});
        Check("world format 2 legacy mode migrates", world && IsLegacyAllOn(world->GetScene().renderFeatures) && warnings.size() == 1);
    }

    // Mutation caught: treating an unrecognized integer as a successful legacy migration.
    {
        FRenderFeatures defaults;
        defaults.rayTracing = true;
        defaults.rayTracedShadows = false;
        std::vector<std::string> warnings;
        FWorldSerializer::SetWarningSink([&](std::string_view message) { warnings.emplace_back(message); });
        std::unique_ptr<UWorld> world(FWorldSerializer::Load(
            "WorldFormat = 2\n\n[World]\nRenderMode = 99\n", defaults));
        FWorldSerializer::SetWarningSink({});
        Check("unknown legacy world mode retains caller defaults without migration diagnostic",
            world && world->GetScene().renderFeatures.rayTracing &&
            !world->GetScene().renderFeatures.rayTracedShadows && warnings.empty());
    }

    // Mutation caught: permissive integer extraction turning malformed legacy
    // values into mode 0/1 and overwriting caller-supplied defaults.
    {
        const char* invalidTokens[] = { "", "garbage", "1junk", "999999999999999999999999" };
        bool preserved = true;
        for (const char* token : invalidTokens)
        {
            FRenderFeatures defaults;
            defaults.rayTracing = true;
            defaults.rayTracedShadows = false;
            defaults.rayTracedGI = false;
            defaults.rayTracedReflections = false;
            defaults.rayTracingBackend = ERayTracingBackend::ComputeGL43;
            std::vector<std::string> warnings;
            FWorldSerializer::SetWarningSink([&](std::string_view message) { warnings.emplace_back(message); });
            std::unique_ptr<UWorld> world(FWorldSerializer::Load(
                std::string("WorldFormat = 2\n\n[World]\nRenderMode = ") + token + "\n", defaults));
            FWorldSerializer::SetWarningSink({});
            preserved &= world && world->GetScene().renderFeatures.rayTracing &&
                !world->GetScene().renderFeatures.rayTracedShadows &&
                !world->GetScene().renderFeatures.rayTracedGI &&
                !world->GetScene().renderFeatures.rayTracedReflections &&
                world->GetScene().renderFeatures.rayTracingBackend == ERayTracingBackend::ComputeGL43 &&
                warnings.empty();
        }
        Check("malformed legacy world mode tokens preserve caller defaults without migration diagnostic", preserved);
    }

    // Mutation caught: omitting a named field, spelling a backend incorrectly,
    // writing legacy state, or allowing hardware primary visibility to turn off.
    {
        UWorld source;
        auto& f = source.GetScene().renderFeatures;
        f.hardwareRaster = false;
        f.rayTracing = true;
        f.rayTracedShadows = false;
        f.rayTracedGI = true;
        f.rayTracedReflections = false;
        f.rayTracingBackend = ERayTracingBackend::ComputeGL43;
        const std::string saved = FWorldSerializer::Save(source);
        std::unique_ptr<UWorld> loaded(FWorldSerializer::Load(saved));
        const auto& got = loaded->GetScene().renderFeatures;
        Check("format 3 save emits complete named normalized features",
            saved.find("WorldFormat = 3") == 0 && saved.find("[RenderFeatures]") != std::string::npos &&
            saved.find("HardwareRaster = 1") != std::string::npos && saved.find("RayTracing = 1") != std::string::npos &&
            saved.find("RayTracedShadows = 0") != std::string::npos && saved.find("RayTracedGI = 1") != std::string::npos &&
            saved.find("RayTracedReflections = 0") != std::string::npos && saved.find("RayTracingBackend = Compute") != std::string::npos &&
            saved.find("RenderMode") == std::string::npos && got.hardwareRaster && got.rayTracing && !got.rayTracedShadows &&
            got.rayTracedGI && !got.rayTracedReflections && got.rayTracingBackend == ERayTracingBackend::ComputeGL43);
        Check("format 3 save-load-save is byte stable", loaded && saved == FWorldSerializer::Save(*loaded));
    }

    // Mutation caught: reading stray legacy state after authoritative named data.
    {
        std::unique_ptr<UWorld> world(FWorldSerializer::Load(
            "WorldFormat = 3\n\n[World]\nRenderMode = 2\n\n[RenderFeatures]\nRayTracing = 0\nRayTracingBackend = Compatible\n"));
        const auto& f = world->GetScene().renderFeatures;
        Check("format 3 named fields override stray legacy mode", !f.rayTracing && f.rayTracingBackend == ERayTracingBackend::CompatibleGL33);
    }

    // Mutation caught: replacing missing named fields with hard-coded defaults
    // instead of the caller's project defaults.
    {
        FRenderFeatures defaults;
        defaults.rayTracing = true;
        defaults.rayTracedShadows = false;
        defaults.rayTracedGI = false;
        defaults.rayTracedReflections = false;
        defaults.rayTracingBackend = ERayTracingBackend::ComputeGL43;
        std::unique_ptr<UWorld> absent(FWorldSerializer::Load("WorldFormat = 3\n\n[World]\nShadingModel = 1\n", defaults));
        std::unique_ptr<UWorld> partial(FWorldSerializer::Load(
            "WorldFormat = 3\n\n[RenderFeatures]\nRayTracing = 0\n", defaults));
        Check("caller project defaults seed absent and partial feature fields",
            absent && absent->GetScene().renderFeatures.rayTracing &&
            absent->GetScene().renderFeatures.rayTracingBackend == ERayTracingBackend::ComputeGL43 &&
            partial && !partial->GetScene().renderFeatures.rayTracing &&
            !partial->GetScene().renderFeatures.rayTracedShadows &&
            partial->GetScene().renderFeatures.rayTracingBackend == ERayTracingBackend::ComputeGL43);
    }

    // Mutation caught: accepting false mandatory hardware or an unknown backend.
    {
        std::vector<std::string> warnings;
        FWorldSerializer::SetWarningSink([&](std::string_view message) { warnings.emplace_back(message); });
        std::unique_ptr<UWorld> world(FWorldSerializer::Load(
            "WorldFormat = 3\n\n[RenderFeatures]\nHardwareRaster = 0\nRayTracingBackend = Vulkan\n"));
        FWorldSerializer::SetWarningSink({});
        Check("invalid named hardware and backend normalize with warnings",
            world && world->GetScene().renderFeatures.hardwareRaster &&
            world->GetScene().renderFeatures.rayTracingBackend == ERayTracingBackend::Auto && warnings.size() == 2);
    }

    // Mutation caught: legacy Mode winning over named fields, or losing normal
    // Display/Startup values while settings migrate.
    {
        Write("render_settings_project.tmp",
            "[Display]\nTitle = Project X\nWidth = 900\nHeight = 500\n"
            "[Render]\nMode = GPU_RT\nHardwareRaster = 1\nRayTracing = 0\nRayTracedShadows = 0\n"
            "RayTracedGI = 1\nRayTracedReflections = 0\nRayTracingBackend = Compatible\n"
            "[Startup]\nDefaultWorld = Maps/Start\n");
        FProjectDescriptor project;
        const bool loaded = project.LoadSettings("render_settings_project.tmp");
        std::remove("render_settings_project.tmp");
        const auto& f = project.defaultRenderFeatures;
        Check("named project settings override legacy mode and preserve boot settings",
            loaded && project.windowTitle == "Project X" && project.width == 900 && project.height == 500 &&
            project.startupWorld == "Maps/Start" && f.hardwareRaster && !f.rayTracing && !f.rayTracedShadows &&
            f.rayTracedGI && !f.rayTracedReflections && f.rayTracingBackend == ERayTracingBackend::CompatibleGL33);
    }

    // Mutation caught: changing any historical project mode mapping.
    {
        const char* modes[] = { "Rasterizer", "GPU_RT", "Hybrid", "CPU_RT" };
        bool mappings = true;
        for (int i = 0; i != 4; ++i)
        {
            Write("render_settings_legacy.tmp", (std::string("[Render]\nMode = ") + modes[i] + "\n").c_str());
            FProjectDescriptor project;
            mappings &= project.LoadSettings("render_settings_legacy.tmp");
            mappings &= project.defaultRenderFeatures.hardwareRaster;
            mappings &= project.defaultRenderFeatures.rayTracing == (i != 0);
            if (i != 0) mappings &= IsLegacyAllOn(project.defaultRenderFeatures);
        }
        std::remove("render_settings_legacy.tmp");
        Check("legacy project modes map to named features", mappings);
    }


    // Mutation caught: dropping support for the original flat .proj key.
    {
        Write("render_settings_flat.tmp", "WindowTitle = Flat Project\nRenderMode = Hybrid\nStartupWorld = OldStart\n");
        FProjectDescriptor project;
        const bool loaded = project.LoadFromFile("render_settings_flat.tmp");
        std::remove("render_settings_flat.tmp");
        Check("legacy flat project render mode remains source-compatible",
            loaded && project.windowTitle == "Flat Project" && project.startupWorld == "OldStart" &&
            IsLegacyAllOn(project.defaultRenderFeatures));
    }

    // Mutation caught: unknown values overwriting safe caller/default values.
    {
        Write("render_settings_unknown.tmp",
            "[Render]\nHardwareRaster = maybe\nRayTracing = perhaps\nRayTracingBackend = Metal\n");
        FProjectDescriptor project;
        project.defaultRenderFeatures.rayTracing = true;
        project.defaultRenderFeatures.rayTracingBackend = ERayTracingBackend::ComputeGL43;
        std::ostringstream warning;
        auto* previous = std::cerr.rdbuf(warning.rdbuf());
        project.LoadSettings("render_settings_unknown.tmp");
        std::cerr.rdbuf(previous);
        std::remove("render_settings_unknown.tmp");
        Check("unknown project backend warns and falls back to Auto while other invalid values retain defaults",
            project.defaultRenderFeatures.hardwareRaster && project.defaultRenderFeatures.rayTracing &&
            project.defaultRenderFeatures.rayTracingBackend == ERayTracingBackend::Auto &&
            warning.str().find("Metal") != std::string::npos && warning.str().find("Auto") != std::string::npos);
    }

    std::printf("=== render settings migration: %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
