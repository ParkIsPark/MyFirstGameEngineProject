// & 'C:\msys64\ucrt64\bin\g++.exe' -std=c++17 -Wall -Wextra -pedantic -I./Engine/Core Test/FeatureLifecycleTest.cpp Engine/Core/FFeatureLifecycle.cpp -o FeatureLifecycleTest.exe; if ($LASTEXITCODE -eq 0) { .\FeatureLifecycleTest.exe }
#include "EngineVersion.h"
#include "FFeatureLifecycle.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

ENGINE_DEPRECATED("compile-only lifecycle macro probe")
void DeprecatedSyntaxProbe()
{
}

namespace
{
void RequireDescriptor(const FFeatureDescriptor& actual,
                       const FFeatureDescriptor& expected)
{
    assert(actual.id == expected.id);
    assert(actual.displayName == expected.displayName);
    assert(actual.lifecycle == expected.lifecycle);
    assert(actual.introducedVersion == expected.introducedVersion);
    assert(actual.deprecatedVersion == expected.deprecatedVersion);
    assert(actual.replacement == expected.replacement);
    assert(actual.removalPolicy == expected.removalPolicy);
    assert(actual.purpose == expected.purpose);
    assert(actual.warning == expected.warning);
}

void CheckExactRegistrationsAndOrder()
{
    const std::vector<FFeatureDescriptor> expected = {
        {
            "renderer.hardware_raster",
            "Hardware Rasterizer",
            EFeatureLifecycle::Stable,
            "2.0",
            "",
            "",
            EFeatureRemovalPolicy::None,
            "Provides normal primary visibility with hardware rasterization.",
            "",
        },
        {
            "renderer.ray_traced_effects",
            "Ray-traced Effects",
            EFeatureLifecycle::Stable,
            "2.0",
            "",
            "",
            EFeatureRemovalPolicy::None,
            "Layers optional secondary shadows, global illumination, and reflections over raster output.",
            "",
        },
        {
            "renderer.rt_compute_gl43",
            "OpenGL 4.3 Compute RT",
            EFeatureLifecycle::Experimental,
            "2.0",
            "",
            "Use renderer.ray_traced_effects with the Compatible path when OpenGL 4.3 Compute is unavailable.",
            EFeatureRemovalPolicy::None,
            "Accelerates ray-traced effects with OpenGL 4.3 Compute.",
            "",
        },
        {
            "renderer.cpu_software",
            "CPU Software Rasterizer",
            EFeatureLifecycle::Deprecated,
            "",
            "2.0",
            "renderer.hardware_raster",
            EFeatureRemovalPolicy::None,
            "Retained for education and raster regression comparison.",
            "Deprecated educational/regression renderer; use Hardware Rasterizer.",
        },
        {
            "renderer.gpu_pure_raytracer",
            "Pure GPU Ray Tracer",
            EFeatureLifecycle::Deprecated,
            "",
            "2.0",
            "Use renderer.hardware_raster with renderer.ray_traced_effects.",
            EFeatureRemovalPolicy::None,
            "Retained for whole-frame ray-tracing comparison and regression coverage.",
            "Deprecated comparison/regression renderer; use hardware raster plus ray-traced effects.",
        },
    };

    const std::vector<FFeatureDescriptor>& actual = FFeatureLifecycleRegistry::All();
    assert(actual.size() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        RequireDescriptor(actual[index], expected[index]);
        assert(&FFeatureLifecycleRegistry::Require(expected[index].id) == &actual[index]);
    }

    assert(&FFeatureLifecycleRegistry::All() == &actual);
    assert(&FFeatureLifecycleRegistry::Require("renderer.hardware_raster") == &actual.front());
}

void CheckVersionAndValidation()
{
    assert(ENGINE_FEATURE_POLICY_VERSION == "2.0");

    bool unknownRejected = false;
    try
    {
        (void)FFeatureLifecycleRegistry::Require("renderer.unknown");
    }
    catch (const std::out_of_range& error)
    {
        const std::string message = error.what();
        unknownRejected = message.find("renderer.unknown") != std::string::npos;
    }
    assert(unknownRejected);

    FFeatureDescriptor valid = FFeatureLifecycleRegistry::All().front();
    FFeatureDescriptor duplicate = valid;
    duplicate.displayName = "Duplicate";

    bool duplicateRejected = false;
    try
    {
        ValidateFeatureDescriptors({valid, duplicate});
    }
    catch (const std::invalid_argument& error)
    {
        const std::string message = error.what();
        duplicateRejected = message.find(valid.id) != std::string::npos;
    }
    assert(duplicateRejected);

    valid.id.clear();
    bool emptyRejected = false;
    try
    {
        ValidateFeatureDescriptors({valid});
    }
    catch (const std::invalid_argument& error)
    {
        emptyRejected = std::string(error.what()).find("empty") != std::string::npos;
    }
    assert(emptyRejected);
}
} // namespace

int main()
{
    CheckExactRegistrationsAndOrder();
    CheckVersionAndValidation();
    std::cout << "FeatureLifecycleTest passed\n";
    return 0;
}
