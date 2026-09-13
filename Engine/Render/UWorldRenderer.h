#pragma once

#include "FGraphicsCapabilities.h"
#include "FRenderFeatures.h"
#include "FRenderPipelinePlan.h"
#include "FRenderQuality.h"
#include "FRenderScene.h"
#include "FRenderTarget.h"

#include <cstdint>
#include <memory>
#include <vector>

class ACamera;
class FOpenGLMeshUploadAdapter;
class UGPUMeshCache;
class UHardwareGBuffer;
class UHardwareRasterizer;
class UWorld;

struct FWorldRenderRequest
{
    const FRenderScene& scene;
    FRenderTarget& target;
    FRenderFeatures features;
    FRenderQuality quality;
    FBackendSelection backendSelection;
    std::vector<ERenderPass> passPlan;
};

// Narrow dispatch seam: tests observe one immutable scene extraction and one
// execution request without OpenGL. The default implementation is the clearly
// named transitional legacy executor replaced by Tasks 7-10.
class IWorldRenderExecutor
{
public:
    virtual ~IWorldRenderExecutor() = default;
    virtual void Init() {}
    virtual void Shutdown() noexcept {}
    virtual bool RequiresOpenGLTargetBinding() const { return false; }
    virtual bool Execute(const FWorldRenderRequest& request) = 0;
};

class UWorldRenderer
{
public:
    UWorldRenderer();
    explicit UWorldRenderer(std::unique_ptr<IWorldRenderExecutor> executor);
    ~UWorldRenderer();
    UWorldRenderer(const UWorldRenderer&) = delete;
    UWorldRenderer& operator=(const UWorldRenderer&) = delete;

    void Init();
    void Shutdown() noexcept;

    bool Render(UWorld& world,
                const ACamera& camera,
                FRenderTarget& target,
                const FRenderFeatures& features,
                const FRenderQuality& quality,
                const FBackendSelection& backendSelection,
                std::uint64_t expectedContextGeneration = 0);

private:
    std::unique_ptr<IWorldRenderExecutor> executor_;
    // Prepared for the Task 8 lighting/composite cutover. Task 7 initializes
    // and owns these real GL resources without executing a hidden extra pass.
    std::unique_ptr<FOpenGLMeshUploadAdapter> hardwareUploadAdapter_;
    std::unique_ptr<UGPUMeshCache> hardwareMeshCache_;
    std::unique_ptr<UHardwareGBuffer> hardwareGBuffer_;
    std::unique_ptr<UHardwareRasterizer> hardwareRasterizer_;
    bool initialized_ = false;
};
