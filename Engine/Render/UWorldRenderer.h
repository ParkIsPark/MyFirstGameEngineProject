#pragma once
#include "URenderer.h"
#include "URasterizer.h"
#include "UMeshRayTracer.h"
#include "UHybridPass.h"
#include "UGBuffer.h"
#include "ThreadPool.h"
#include "USkyHDRI.h"
#include "FRenderQuality.h"
#include "FRenderFeatures.h"

class UWorld;

// Temporary bridge while the legacy render implementations remain underneath
// named settings. Normal settings can select only RasterOnly (0) or Hybrid (2).
namespace RenderCompatibility
{
    inline int LegacyModeForNamedFeatures(const FRenderFeatures& features)
    {
        return features.rayTracing ? 2 : 0;
    }
}

// ---------------------------------------------------------------------------
// UWorldRenderer (P9) — renders named world features into the currently bound
// framebuffer. During migration it owns the legacy passes and routes RT-off to
// Rasterizer and RT-on to Hybrid; Pure GPU RT is not reachable from normal settings.
//
// Raster mode shades to scene.outputImage (URenderer::RasterShaded) then blits
// with glDrawPixels; the GPU modes draw a fullscreen pass. The caller sets up the
// world camera (orientation + FOV/aspect) before calling Render().
// ---------------------------------------------------------------------------
class UWorldRenderer
{
public:
    void Init();                                       // compile GPU passes (GL ready)
    void Render(UWorld& world, const FRenderFeatures& features, int w, int h);

private:
    void renderRaster(UWorld& world, int w, int h);
    void renderGPU(UWorld& world, int mode, int w, int h);

    URenderer      raster_;       // RasterShaded (mode 0)
    URasterizer    rast_;         // G-buffer fill (hybrid)
    UMeshRayTracer worldRT_;      // GPU ray trace (mode 1)
    UHybridPass    hybrid_;       // hybrid shadow pass (mode 2)
    UGBuffer       gbuf_;
    ThreadPool     pool_;
    USkyHDRI       sky_;

    size_t rtSig_ = 0, hySig_ = 0;
    bool   rtUp_ = false, hyUp_ = false;
    bool   ready_ = false;

    FRenderQuality quality_;          // Game render profile (Config/GameSettings.ini)
    bool   qualityLoaded_ = false;
};
