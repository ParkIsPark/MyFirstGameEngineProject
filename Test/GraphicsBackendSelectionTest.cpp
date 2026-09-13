// & 'C:\msys64\ucrt64\bin\g++.exe' -std=c++17 -Wall -Wextra -pedantic -I./Engine/Render Test/GraphicsBackendSelectionTest.cpp Engine/Render/FGraphicsCapabilities.cpp -o GraphicsBackendSelectionTest.exe; if ($LASTEXITCODE -eq 0) { .\GraphicsBackendSelectionTest.exe }
#include "FGraphicsCapabilities.h"

#include <cassert>
#include <iostream>
#include <string>

namespace
{
class FProbeSource final : public IGraphicsCapabilitySource
{
public:
    bool integerVersionAvailable = true;
    int integerMajor = 0;
    int integerMinor = 0;
    const char* versionString = nullptr;
    bool computeExtension = false;
    bool storageExtension = false;
    bool entryPoints = false;

    bool QueryIntegerVersion(int& major, int& minor) const override
    {
        major = integerMajor;
        minor = integerMinor;
        return integerVersionAvailable;
    }

    const char* QueryVersionString() const override { return versionString; }

    bool HasExtension(const char* extensionName) const override
    {
        if (std::string(extensionName) == "GL_ARB_compute_shader") return computeExtension;
        if (std::string(extensionName) == "GL_ARB_shader_storage_buffer_object")
            return storageExtension;
        return false;
    }

    bool HasRequiredComputeEntryPoints() const override { return entryPoints; }
};

FGraphicsCapabilities Capabilities(int major,
                                   int minor,
                                   bool computeShaders = false,
                                   bool shaderStorageBuffers = false,
                                   bool requiredComputeEntryPoints = false)
{
    FGraphicsCapabilities result;
    result.major = major;
    result.minor = minor;
    result.computeShaders = computeShaders;
    result.shaderStorageBuffers = shaderStorageBuffers;
    result.requiredComputeEntryPoints = requiredComputeEntryPoints;
    return result;
}

void CheckVersionBoundary()
{
    assert(!Capabilities(3, 2).MeetsOpenGL33());
    assert(Capabilities(3, 3).MeetsOpenGL33());
    assert(Capabilities(4, 0).MeetsOpenGL33());
    assert(!Capabilities(4, 2, true, true, true).SupportsComputeBackend());
    assert(Capabilities(4, 3, true, true, true).SupportsComputeBackend());
}

void CheckCapabilityProbeUsesIntegerVersionAndSafeStringFallback()
{
    FProbeSource integerSource;
    integerSource.integerMajor = 4;
    integerSource.integerMinor = 3;
    integerSource.entryPoints = true;

    const FGraphicsCapabilities integerResult = ProbeGraphicsCapabilities(integerSource);
    assert(integerResult.major == 4);
    assert(integerResult.minor == 3);
    assert(integerResult.computeShaders);
    assert(integerResult.shaderStorageBuffers);
    assert(integerResult.requiredComputeEntryPoints);

    FProbeSource stringSource;
    stringSource.integerVersionAvailable = false;
    stringSource.versionString = "4.3.0 Vendor Driver";
    stringSource.entryPoints = true;

    const FGraphicsCapabilities stringResult = ProbeGraphicsCapabilities(stringSource);
    assert(stringResult.major == 4);
    assert(stringResult.minor == 3);
    assert(stringResult.SupportsComputeBackend());

    FProbeSource extensionSource;
    extensionSource.integerMajor = 3;
    extensionSource.integerMinor = 3;
    extensionSource.computeExtension = true;
    extensionSource.storageExtension = true;
    extensionSource.entryPoints = true;
    const FGraphicsCapabilities extensionResult = ProbeGraphicsCapabilities(extensionSource);
    assert(extensionResult.computeShaders);
    assert(extensionResult.shaderStorageBuffers);
    assert(!extensionResult.SupportsComputeBackend());

    FProbeSource malformedSource;
    malformedSource.integerVersionAvailable = false;
    malformedSource.versionString = "OpenGL ES vendor text";
    const FGraphicsCapabilities malformedResult = ProbeGraphicsCapabilities(malformedSource);
    assert(malformedResult.major == 0);
    assert(malformedResult.minor == 0);
    assert(!malformedResult.MeetsOpenGL33());

    FProbeSource overflowSource;
    overflowSource.integerVersionAvailable = false;
    overflowSource.versionString = "999999999999999999999.3 invalid";
    const FGraphicsCapabilities overflowResult = ProbeGraphicsCapabilities(overflowSource);
    assert(overflowResult.major == 0);
    assert(overflowResult.minor == 0);
}

void CheckBelowMinimumIsRejected()
{
    const FBackendSelection selection =
        SelectRayTracingBackend(ERayTracingBackend::Auto, Capabilities(3, 2));

    assert(selection.requested == ERayTracingBackend::Auto);
    assert(selection.selected == ERayTracingBackend::CompatibleGL33);
    assert(!selection.available);
    assert(!selection.rayTracingEnabled);
    assert(selection.fallbackReason.find("OpenGL 3.3") != std::string::npos);
}

void CheckCompatibleOnOpenGL33()
{
    const FBackendSelection selection = SelectRayTracingBackend(
        ERayTracingBackend::CompatibleGL33, Capabilities(3, 3));

    assert(selection.requested == ERayTracingBackend::CompatibleGL33);
    assert(selection.selected == ERayTracingBackend::CompatibleGL33);
    assert(selection.available);
    assert(selection.rayTracingEnabled);
    assert(selection.fallbackReason.empty());
}

void CheckAutoSelection()
{
    const FBackendSelection fallback =
        SelectRayTracingBackend(ERayTracingBackend::Auto, Capabilities(3, 3));
    assert(fallback.selected == ERayTracingBackend::CompatibleGL33);
    assert(fallback.available);
    assert(fallback.rayTracingEnabled);
    assert(fallback.fallbackReason.find("Compute") != std::string::npos);
    assert(fallback.fallbackReason.find("Compatible") != std::string::npos);

    const FBackendSelection compute = SelectRayTracingBackend(
        ERayTracingBackend::Auto, Capabilities(4, 3, true, true, true));
    assert(compute.selected == ERayTracingBackend::ComputeGL43);
    assert(compute.available);
    assert(compute.rayTracingEnabled);
    assert(compute.fallbackReason.empty());
}

void CheckEveryComputePrerequisite()
{
    const FGraphicsCapabilities missing[] = {
        Capabilities(4, 2, true, true, true),
        Capabilities(4, 3, false, true, true),
        Capabilities(4, 3, true, false, true),
        Capabilities(4, 3, true, true, false),
    };
    const char* expectedReasons[] = {
        "OpenGL 4.3",
        "compute shader",
        "shader storage buffer",
        "entry points",
    };

    for (int index = 0; index < 4; ++index)
    {
        const FGraphicsCapabilities& capabilities = missing[index];
        assert(!capabilities.SupportsComputeBackend());
        const FBackendSelection selection =
            SelectRayTracingBackend(ERayTracingBackend::Auto, capabilities);
        assert(selection.selected == ERayTracingBackend::CompatibleGL33);
        assert(selection.available);
        assert(selection.rayTracingEnabled);
        assert(selection.fallbackReason.find(expectedReasons[index]) != std::string::npos);
    }
}

void CheckForcedCompute()
{
    const FBackendSelection supported = SelectRayTracingBackend(
        ERayTracingBackend::ComputeGL43, Capabilities(4, 3, true, true, true));
    assert(supported.requested == ERayTracingBackend::ComputeGL43);
    assert(supported.selected == ERayTracingBackend::ComputeGL43);
    assert(supported.available);
    assert(supported.rayTracingEnabled);
    assert(supported.fallbackReason.empty());

    const FBackendSelection unsupported = SelectRayTracingBackend(
        ERayTracingBackend::ComputeGL43, Capabilities(4, 3, true, false, true));
    assert(unsupported.requested == ERayTracingBackend::ComputeGL43);
    assert(unsupported.selected == ERayTracingBackend::ComputeGL43);
    assert(!unsupported.available);
    assert(!unsupported.rayTracingEnabled);
    assert(unsupported.fallbackReason.find("shader storage buffer") != std::string::npos);
    assert(unsupported.fallbackReason.find("disabled") != std::string::npos);

    const FBackendSelection unsupportedVersion = SelectRayTracingBackend(
        ERayTracingBackend::ComputeGL43, Capabilities(3, 3));
    assert(unsupportedVersion.selected == ERayTracingBackend::ComputeGL43);
    assert(!unsupportedVersion.available);
    assert(!unsupportedVersion.rayTracingEnabled);
    assert(unsupportedVersion.fallbackReason.find("OpenGL 4.3") != std::string::npos);
}

void CheckSelectionIsDeterministic()
{
    const FGraphicsCapabilities capabilities = Capabilities(3, 3);
    const FBackendSelection first =
        SelectRayTracingBackend(ERayTracingBackend::Auto, capabilities);
    const FBackendSelection second =
        SelectRayTracingBackend(ERayTracingBackend::Auto, capabilities);

    assert(first.requested == second.requested);
    assert(first.selected == second.selected);
    assert(first.available == second.available);
    assert(first.rayTracingEnabled == second.rayTracingEnabled);
    assert(first.fallbackReason == second.fallbackReason);
}

void CheckWarningDedupeDistinguishesRequestAndReason()
{
    const FGraphicsCapabilities capabilities = Capabilities(3, 3);
    FBackendWarningDeduplicator warnings;

    const FBackendSelection automatic =
        SelectRayTracingBackend(ERayTracingBackend::Auto, capabilities);
    const FBackendSelection forced =
        SelectRayTracingBackend(ERayTracingBackend::ComputeGL43, capabilities);

    assert(warnings.ShouldEmit(automatic));
    assert(warnings.ShouldEmit(forced));
    assert(!warnings.ShouldEmit(forced));
    assert(warnings.EmittedCount() == 2);

    warnings.Reset();
    assert(warnings.EmittedCount() == 0);
    assert(warnings.ShouldEmit(automatic));
}
} // namespace

int main()
{
    CheckVersionBoundary();
    CheckCapabilityProbeUsesIntegerVersionAndSafeStringFallback();
    CheckBelowMinimumIsRejected();
    CheckCompatibleOnOpenGL33();
    CheckAutoSelection();
    CheckEveryComputePrerequisite();
    CheckForcedCompute();
    CheckSelectionIsDeterministic();
    CheckWarningDedupeDistinguishesRequestAndReason();
    std::cout << "GraphicsBackendSelectionTest passed\n";
    return 0;
}
