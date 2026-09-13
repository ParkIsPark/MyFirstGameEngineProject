#pragma once

#include "FRenderFeatures.h"

#include <cstddef>
#include <string>
#include <unordered_set>

struct FGraphicsCapabilities
{
    int major = 0;
    int minor = 0;
    bool computeShaders = false;
    bool shaderStorageBuffers = false;
    bool requiredComputeEntryPoints = false;

    bool MeetsOpenGL33() const;
    bool SupportsComputeBackend() const;
};

class IGraphicsCapabilitySource
{
public:
    virtual ~IGraphicsCapabilitySource() = default;

    virtual bool QueryIntegerVersion(int& major, int& minor) const = 0;
    virtual const char* QueryVersionString() const = 0;
    virtual bool HasExtension(const char* extensionName) const = 0;
    virtual bool HasRequiredComputeEntryPoints() const = 0;
};

FGraphicsCapabilities ProbeGraphicsCapabilities(const IGraphicsCapabilitySource& source);

struct FBackendSelection
{
    ERayTracingBackend requested = ERayTracingBackend::Auto;
    ERayTracingBackend selected = ERayTracingBackend::CompatibleGL33;
    bool available = false;
    bool rayTracingEnabled = false;
    std::string fallbackReason;
};

FBackendSelection SelectRayTracingBackend(
    ERayTracingBackend requested,
    const FGraphicsCapabilities& capabilities);

// Session-local diagnostic seam. Different authored requests and different
// failure reasons each earn one warning; identical repeats stay quiet.
class FBackendWarningDeduplicator
{
public:
    bool ShouldEmit(const FBackendSelection& selection);
    void Reset() { emittedKeys_.clear(); }
    std::size_t EmittedCount() const { return emittedKeys_.size(); }

private:
    std::unordered_set<std::string> emittedKeys_;
};
