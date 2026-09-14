#pragma once

#include "FGraphicsCapabilities.h"
#include "FRenderFeatures.h"
#include "FRenderOutputs.h"
#include "FRenderQuality.h"

#include <cstdint>
#include <memory>
#include <string>

struct FRenderScene;
class UHardwareGBuffer;

struct FRayEffectInputs
{
    const UHardwareGBuffer* gbuffer = nullptr;
    const FRenderScene* scene = nullptr;
    const FRasterLightingOutput* rasterLighting = nullptr;
    FRenderFeatures features;
    FRenderQuality quality;
    unsigned environmentTexture = 0;
    int width = 0;
    int height = 0;
    std::uint64_t contextGeneration = 0;
};
struct FRayTracingBackendStats
{
    std::uint64_t resourceAllocations = 0;
    std::uint64_t renderCalls = 0;
    std::uint64_t rayDraws = 0;
    std::uint64_t sceneUploads = 0;
    std::uint64_t blasUploads = 0;
    std::uint64_t instanceUploads = 0;
    std::uint64_t materialUploads = 0;
    std::uint64_t outputAllocations = 0;
    std::uint64_t releasedResources = 0;
    std::uint64_t abandonedResources = 0;
    std::size_t residentBLAS = 0;
    std::size_t ownedOutputTextures = 0;
};

class IRayTracingBackend
{
public:
    virtual ~IRayTracingBackend() = default;
    virtual bool Init(std::uint64_t contextGeneration,
                      std::string* diagnostic = nullptr) = 0;
    virtual bool RenderEffects(const FRayEffectInputs& inputs,
                               FRayEffectOutputs& outputs,
                               std::string* diagnostic = nullptr) = 0;
    virtual void Shutdown() noexcept = 0;
    virtual const FRayTracingBackendStats& Stats() const = 0;
};

class IRayTracingBackendFactory
{
public:
    virtual ~IRayTracingBackendFactory() = default;
    virtual std::unique_ptr<IRayTracingBackend> Create(
        ERayTracingBackend backend, std::string* diagnostic = nullptr) = 0;
};

class IRayEffectsWarningSink
{
public:
    virtual ~IRayEffectsWarningSink() = default;
    virtual void Warn(const std::string& warning) = 0;
};

struct FRayEffectsSchedulerStats
{
    std::uint64_t factoryCalls = 0;
    std::uint64_t backendInitializations = 0;
    std::uint64_t backendCalls = 0;
    std::uint64_t resourceAllocations = 0;
    std::uint64_t sceneUploads = 0;
    std::uint64_t warnings = 0;
};

// Pure orchestration/latching policy. The factory and backend are injected so
// selection behavior is fully testable without creating an OpenGL context.
class FRayEffectsScheduler
{
public:
    FRayEffectsScheduler(IRayTracingBackendFactory& factory,
                         IRayEffectsWarningSink& warnings);
    ~FRayEffectsScheduler();

    bool Execute(const FRayEffectInputs& inputs,
                 const FBackendSelection& selection,
                 FRayEffectOutputs& outputs);
    void Shutdown() noexcept;
    const FRayEffectsSchedulerStats& Stats() const { return stats_; }
    IRayTracingBackend* ActiveBackend() const { return backend_.get(); }

private:
    bool EnsureBackend(ERayTracingBackend backend,
                       std::uint64_t contextGeneration,
                       std::string& diagnostic);
    void WarnOnce(const std::string& key, const std::string& message);
    void ResetForContext(std::uint64_t contextGeneration) noexcept;

    IRayTracingBackendFactory& factory_;
    IRayEffectsWarningSink& warnings_;
    std::unique_ptr<IRayTracingBackend> backend_;
    ERayTracingBackend activeKind_ = ERayTracingBackend::Auto;
    std::uint64_t contextGeneration_ = 0;
    std::string failureKey_;
    std::string warningKeys_;
    FRayEffectsSchedulerStats stats_;
};
