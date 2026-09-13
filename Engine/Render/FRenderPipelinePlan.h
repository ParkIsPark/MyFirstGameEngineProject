#pragma once

#include "FRenderFeatures.h"

#include <vector>

enum class ERenderPass
{
    HardwareGBuffer,
    RasterLighting,
    RayTracedEffects,
    Composite,
};

std::vector<ERenderPass> BuildRenderPipelinePlan(const FRenderFeatures& features);
