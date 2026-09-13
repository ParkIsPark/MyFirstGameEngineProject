#include "FGraphicsCapabilities.h"

#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <limits>

namespace
{
bool VersionAtLeast(int major, int minor, int requiredMajor, int requiredMinor)
{
    return major > requiredMajor || (major == requiredMajor && minor >= requiredMinor);
}

bool ParseVersion(const char* text, int& major, int& minor)
{
    if (!text) return false;

    while (*text && !std::isdigit(static_cast<unsigned char>(*text))) ++text;
    if (!*text) return false;

    errno = 0;
    char* end = nullptr;
    const long parsedMajor = std::strtol(text, &end, 10);
    if (end == text || *end != '.' || errno == ERANGE
        || parsedMajor > std::numeric_limits<int>::max()) return false;

    text = end + 1;
    errno = 0;
    const long parsedMinor = std::strtol(text, &end, 10);
    if (end == text || errno == ERANGE || parsedMajor < 0 || parsedMinor < 0
        || parsedMinor > std::numeric_limits<int>::max()) return false;

    major = static_cast<int>(parsedMajor);
    minor = static_cast<int>(parsedMinor);
    return true;
}

std::string ComputeUnavailabilityReason(const FGraphicsCapabilities& capabilities)
{
    if (!VersionAtLeast(capabilities.major, capabilities.minor, 4, 3))
        return "the Compute backend requires OpenGL 4.3 or newer";
    if (!capabilities.computeShaders)
        return "compute shader support is unavailable";
    if (!capabilities.shaderStorageBuffers)
        return "shader storage buffer support is unavailable";
    if (!capabilities.requiredComputeEntryPoints)
        return "one or more required OpenGL Compute entry points are unavailable";
    return "the OpenGL Compute backend is unavailable";
}
} // namespace

bool FGraphicsCapabilities::MeetsOpenGL33() const
{
    return VersionAtLeast(major, minor, 3, 3);
}

bool FGraphicsCapabilities::SupportsComputeBackend() const
{
    return VersionAtLeast(major, minor, 4, 3)
        && computeShaders
        && shaderStorageBuffers
        && requiredComputeEntryPoints;
}

FGraphicsCapabilities ProbeGraphicsCapabilities(const IGraphicsCapabilitySource& source)
{
    FGraphicsCapabilities capabilities;
    if (!source.QueryIntegerVersion(capabilities.major, capabilities.minor)
        || capabilities.major <= 0)
    {
        capabilities.major = 0;
        capabilities.minor = 0;
        ParseVersion(source.QueryVersionString(), capabilities.major, capabilities.minor);
    }

    const bool coreCompute = VersionAtLeast(capabilities.major, capabilities.minor, 4, 3);
    capabilities.computeShaders =
        coreCompute || source.HasExtension("GL_ARB_compute_shader");
    capabilities.shaderStorageBuffers =
        coreCompute || source.HasExtension("GL_ARB_shader_storage_buffer_object");
    capabilities.requiredComputeEntryPoints = source.HasRequiredComputeEntryPoints();
    return capabilities;
}

FBackendSelection SelectRayTracingBackend(
    ERayTracingBackend requested,
    const FGraphicsCapabilities& capabilities)
{
    FBackendSelection result;
    result.requested = requested;
    result.selected = requested == ERayTracingBackend::Auto
        ? ERayTracingBackend::CompatibleGL33
        : requested;

    if (!capabilities.MeetsOpenGL33())
    {
        result.fallbackReason =
            "Ray tracing requires OpenGL 3.3 or newer; ray tracing is disabled for this session.";
        return result;
    }

    if (requested == ERayTracingBackend::CompatibleGL33)
    {
        result.available = true;
        result.rayTracingEnabled = true;
        return result;
    }

    if (capabilities.SupportsComputeBackend())
    {
        result.selected = ERayTracingBackend::ComputeGL43;
        result.available = true;
        result.rayTracingEnabled = true;
        return result;
    }

    const std::string unavailable = ComputeUnavailabilityReason(capabilities);
    if (requested == ERayTracingBackend::ComputeGL43)
    {
        result.selected = ERayTracingBackend::ComputeGL43;
        result.fallbackReason = unavailable + "; ray tracing is disabled for this session.";
        return result;
    }

    result.selected = ERayTracingBackend::CompatibleGL33;
    result.available = true;
    result.rayTracingEnabled = true;
    result.fallbackReason = unavailable + "; using the Compatible OpenGL 3.3 backend.";
    return result;
}

bool FBackendWarningDeduplicator::ShouldEmit(const FBackendSelection& selection)
{
    if (selection.fallbackReason.empty()) return false;
    const std::string key = std::to_string(static_cast<int>(selection.requested))
        + ":" + selection.fallbackReason;
    return emittedKeys_.insert(key).second;
}
