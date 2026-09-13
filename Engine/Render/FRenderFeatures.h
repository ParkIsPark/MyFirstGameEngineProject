#pragma once

enum class ERayTracingBackend
{
    Auto,
    CompatibleGL33,
    ComputeGL43,
};

enum class ELegacyRendererOverride
{
    None,
    SoftwareRasterizer,
    PureGPURayTracer,
};

struct FRenderFeatures
{
    bool hardwareRaster = true;
    bool rayTracing = false;
    bool rayTracedShadows = true;
    bool rayTracedGI = true;
    bool rayTracedReflections = true;
    ERayTracingBackend rayTracingBackend = ERayTracingBackend::Auto;
};
