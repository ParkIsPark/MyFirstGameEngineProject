// Current complete recipe: powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name RenderPipelinePlanTest
// Prerequisite: Engine.sln Debug|Win32. Older direct compile examples follow.
// & 'C:\msys64\ucrt64\bin\g++.exe' -std=c++17 -Wall -Wextra -pedantic -I./Engine/Render Test/RenderPipelinePlanTest.cpp Engine/Render/FRenderPipelinePlan.cpp -o RenderPipelinePlanTest.exe; if ($LASTEXITCODE -eq 0) { .\RenderPipelinePlanTest.exe }
#include "FRenderPipelinePlan.h"

#include <cassert>
#include <initializer_list>
#include <iostream>
#include <vector>

namespace
{
void RequirePlan(const FRenderFeatures& features,
                 std::initializer_list<ERenderPass> expected)
{
    const std::vector<ERenderPass> actual = BuildRenderPipelinePlan(features);
    assert(actual == std::vector<ERenderPass>(expected));
}

void CheckRasterOnlyPlans()
{
    const std::initializer_list<ERenderPass> rasterOnly = {
        ERenderPass::HardwareGBuffer,
        ERenderPass::RasterLighting,
        ERenderPass::Composite,
    };

    RequirePlan(FRenderFeatures{}, rasterOnly);

    FRenderFeatures mandatoryHardwareRaster;
    mandatoryHardwareRaster.hardwareRaster = false;
    RequirePlan(mandatoryHardwareRaster, rasterOnly);

    FRenderFeatures disabled;
    disabled.rayTracing = false;
    disabled.rayTracedShadows = true;
    disabled.rayTracedGI = true;
    disabled.rayTracedReflections = true;
    disabled.rayTracedTranslucency = true;
    RequirePlan(disabled, rasterOnly);

    FRenderFeatures noEffects;
    noEffects.rayTracing = true;
    noEffects.rayTracedShadows = false;
    noEffects.rayTracedGI = false;
    noEffects.rayTracedReflections = false;
    noEffects.rayTracedTranslucency = false;
    RequirePlan(noEffects, rasterOnly);
}

void CheckEachRayEffectSchedulesOnePass()
{
    const std::initializer_list<ERenderPass> withRayEffects = {
        ERenderPass::HardwareGBuffer,
        ERenderPass::RasterLighting,
        ERenderPass::RayTracedEffects,
        ERenderPass::Composite,
    };

    FRenderFeatures shadowsOnly;
    shadowsOnly.rayTracing = true;
    shadowsOnly.rayTracedShadows = true;
    shadowsOnly.rayTracedGI = false;
    shadowsOnly.rayTracedReflections = false;
    shadowsOnly.rayTracedTranslucency = false;
    RequirePlan(shadowsOnly, withRayEffects);

    FRenderFeatures giOnly = shadowsOnly;
    giOnly.rayTracedShadows = false;
    giOnly.rayTracedGI = true;
    RequirePlan(giOnly, withRayEffects);

    FRenderFeatures reflectionsOnly = shadowsOnly;
    reflectionsOnly.rayTracedShadows = false;
    reflectionsOnly.rayTracedReflections = true;
    RequirePlan(reflectionsOnly, withRayEffects);

    FRenderFeatures translucencyOnly = shadowsOnly;
    translucencyOnly.rayTracedShadows = false;
    translucencyOnly.rayTracedTranslucency = true;
    RequirePlan(translucencyOnly, withRayEffects);

    FRenderFeatures allEffects;
    allEffects.rayTracing = true;
    RequirePlan(allEffects, withRayEffects);
}

void CheckBackendDoesNotAffectOrdering()
{
    const std::vector<ERenderPass> expected = {
        ERenderPass::HardwareGBuffer,
        ERenderPass::RasterLighting,
        ERenderPass::RayTracedEffects,
        ERenderPass::Composite,
    };

    for (const ERayTracingBackend backend : {
             ERayTracingBackend::Auto,
             ERayTracingBackend::CompatibleGL33,
             ERayTracingBackend::ComputeGL43,
         })
    {
        FRenderFeatures features;
        features.rayTracing = true;
        features.rayTracingBackend = backend;
        assert(BuildRenderPipelinePlan(features) == expected);
    }
}

void CheckPlansAreFreshAndDeterministic()
{
    FRenderFeatures features;
    features.rayTracing = true;

    std::vector<ERenderPass> first = BuildRenderPipelinePlan(features);
    const std::vector<ERenderPass> second = BuildRenderPipelinePlan(features);
    first.clear();

    assert(BuildRenderPipelinePlan(features) == second);
    assert(second.size() == 4);
}
} // namespace

int main()
{
    CheckRasterOnlyPlans();
    CheckEachRayEffectSchedulesOnePass();
    CheckBackendDoesNotAffectOrdering();
    CheckPlansAreFreshAndDeterministic();
    std::cout << "RenderPipelinePlanTest passed\n";
    return 0;
}
