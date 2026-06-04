#pragma once
#include "URenderer.h"
#include "URasterizer.h"
#include "UMeshRayTracer.h"
#include "UHybridPass.h"
#include "UGBuffer.h"
#include "ThreadPool.h"
#include "USkyHDRI.h"

class UWorld;

// ---------------------------------------------------------------------------
// UWorldRenderer (P9) — renders a UWorld in any mode (0 Rasterizer / 1 GPU RT /
// 2 Hybrid) into the CURRENTLY BOUND framebuffer at viewport (0,0,w,h). Owns the
// GPU passes + the camera-independent geometry-upload cache. Used by GameEngine
// (default framebuffer) so the standalone game honors the world's render mode;
// reusable by the editor.
//
// Raster mode shades to scene.outputImage (URenderer::RasterShaded) then blits
// with glDrawPixels; the GPU modes draw a fullscreen pass. The caller sets up the
// world camera (orientation + FOV/aspect) before calling Render().
// ---------------------------------------------------------------------------
class UWorldRenderer
{
public:
    void Init();                                       // compile GPU passes (GL ready)
    void Render(UWorld& world, int mode, int w, int h);

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
};
