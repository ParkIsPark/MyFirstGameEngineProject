#pragma once
#include <vector>

#include "URasterizer.h"
#include "UFrameBuffer.h"
#include "UGBuffer.h"
#include "ThreadPool.h"
#include "FRenderShowFlag.h"

class UWorld;
class UScene;
class ACamera;
class USkyHDRI;

// Top-level render mode. CPU-only ray tracing was removed (page 1), so the
// modes are: rasterizer (CPU primary visibility), pure GPU ray trace, and the
// hybrid (CPU raster G-buffer -> GPU shadow/reflection).
enum class ERenderMode { RasterOnly, GPURayTrace, Hybrid };

// A frame is a sequence of these stages. Used both as the execution plan and
// as the dispatch record for testing (Plan() is pure / GL-free).
enum class ERenderStage { Raster, GBuffer, Upload, GPUShadow, GPURayTrace };

// ---------------------------------------------------------------------------
// URenderer (page 4) — render orchestrator. Engine::Render() calls this one
// class; it owns the rasterizer + ray tracer + G-buffer and assembles a frame
// per mode. The GPU shadow/reflection pass uses a fragment shader (#version 330)
// rather than a compute shader, so no GL 4.3 is assumed on the target machine.
// ---------------------------------------------------------------------------
class URenderer
{
public:
    // Pure dispatch plan for a mode (no GL) -- the ordered stage list.
    static std::vector<ERenderStage> Plan(ERenderMode mode);

    void Render(UWorld& world, ERenderMode mode);

    // CPU shaded raster (HW6 Q1-Q3): rasterize every actor's mesh with the
    // flag's shading model (Flat/Gouraud/Phong) + Blinn-Phong + gamma, writing
    // scene.outputImage. depthView -> grayscale depth instead.
    void RasterShaded(UWorld& world, const FRenderShowFlag& flag, const USkyHDRI* sky = nullptr);

    // Last executed plan, for logging / dispatch tests.
    const std::vector<ERenderStage>& LastPlan() const { return lastPlan_; }

    UFrameBuffer& FrameBuffer() { return fb_; }
    UGBuffer&     GBuffer()     { return gbuffer_; }

private:
    // CPU primary visibility of every actor's mesh -> outputImage (flat albedo).
    void RasterWorld (UScene& scene, const ACamera& cam, int nx, int ny);
    // CPU primary visibility -> G-buffer (world pos / normal / albedo / depth).
    void GBufferWorld(UScene& scene, const ACamera& cam, int nx, int ny);

    URasterizer  raster_;
    UFrameBuffer fb_;
    UGBuffer     gbuffer_;
    ThreadPool   pool_;                       // worker pool for the lit shading pass
    std::vector<ERenderStage> lastPlan_;

public:
    bool multithread = true;                  // toggle for the single-vs-MT test
};
