#pragma once
#include <vector>
#include <string>
#include "AActor.h"
#include "../Light/ALight.h"
#include "UPostProcessFilter.h"
#include "../Render/FRenderFeatures.h"

class UScene
{
public:
    /** Screen dimensions */
    int width, height;

    std::vector<float> outputImage;

    std::vector<AActor*> Actors;
    std::vector<ALight*> Lights;

    // World render option (EShadingModel order: 0 Flat, 1 Gouraud, 2 Phong).
    // Stored as int to avoid a Core->Rasterizer dependency; serialized in [World].
    int shadingModel = 2;

    // Authoritative per-world renderer configuration. Hardware raster primary
    // visibility is mandatory; optional ray effects are independently authored.
    FRenderFeatures renderFeatures;

    // Equirectangular .hdr environment map path (empty = procedural gradient).
    // Sampled by the GPU paths' skyColor() for background + ambient. [World].
    std::string skyHDRI;

    // Retained educational software-renderer post-process data.
    // Default values are identity (no change).
    UPostProcessFilter filter;

    UScene();
    ~UScene();
};
