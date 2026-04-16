#pragma once
#include "URayTracingPass.h"

class UScene;
class ACamera;

// ---------------------------------------------------------------------------
// PostProcessPass
//
// Responsibility: trace loop + screen entry point with AA and gamma.
//   GLSL  : trace() — reflection loop with ambient + shade() calls;
//            main() — stratified n×n AA grid, gamma correction.
//   Upload: uAAGrid (n, so n*n samples), uEnableGamma, uGamma.
//
// Call setSettings() before each RenderFrame to configure AA and gamma.
// When aaGrid=1 the inner loop collapses to a single center sample.
// ---------------------------------------------------------------------------
class PostProcessPass : public URayTracingPass
{
public:
    PostProcessPass() = default;

    // Configure before each frame.
    // aaGrid  : grid dimension (total samples = aaGrid*aaGrid; 1 = no AA)
    // enableGamma / gamma : gamma correction knobs
    void setSettings(int aaGrid, bool enableGamma, float gamma);

    std::string getGLSL() const override;

    void uploadUniforms(unsigned int prog,
                        const UScene& scene,
                        const ACamera& cam) const override;

private:
    int   aaGrid_      = 1;
    bool  enableGamma_ = false;
    float gamma_       = 2.2f;
};
