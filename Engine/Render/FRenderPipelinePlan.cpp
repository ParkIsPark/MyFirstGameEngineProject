#include "FRenderPipelinePlan.h"

std::vector<ERenderPass> BuildRenderPipelinePlan(const FRenderFeatures& features)
{
    std::vector<ERenderPass> plan = {
        ERenderPass::HardwareGBuffer,
        ERenderPass::RasterLighting,
    };

    const bool anyRayTracedEffect = features.rayTracedShadows ||
                                    features.rayTracedGI ||
                                    features.rayTracedReflections;
    if (features.rayTracing && anyRayTracedEffect)
    {
        plan.push_back(ERenderPass::RayTracedEffects);
    }

    plan.push_back(ERenderPass::Composite);
    return plan;
}
