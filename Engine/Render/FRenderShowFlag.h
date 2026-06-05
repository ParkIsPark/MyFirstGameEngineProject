#pragma once
#include "URasterizer.h"   // EShadingModel

// ---------------------------------------------------------------------------
// FRenderShowFlag (P4) — composable render options (Unreal show-flags style).
//
// The CPU raster path consumes `shading` (Flat/Gouraud/Phong) + `depthView`.
// The other bits scaffold the GPU pass composition unified in a later phase
// (rayTrace / shadows / envLighting). Worlds store their chosen `shading`
// ([World] ShadingModel); the editor toolbar drives it live.
// ---------------------------------------------------------------------------
struct FRenderShowFlag
{
    bool rasterize   = true;
    bool rayTrace    = false;
    bool shadows     = false;
    bool envLighting = false;
    bool depthView   = false;

    EShadingModel shading = EShadingModel::Phong;

    float ambientStrength = 1.0f;   // scales the CPU raster's environment ambient
};
