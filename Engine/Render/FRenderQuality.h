#pragma once

// ---------------------------------------------------------------------------
// FRenderQuality — render-quality knobs shared by the editor viewport and the
// standalone game. The editor keeps two independent profiles (Editor vs Game)
// so the editing view and the played/built game can render differently; the
// game profile is written to Config/GameSettings.ini and read back by the game.
// ---------------------------------------------------------------------------
struct FRenderQuality
{
    int   ssaa            = 1;     // super-sample AA factor (1 or 2)
    float ambientStrength = 1.0f;  // raster environment-ambient scale
    int   giSamples       = 8;     // hemisphere GI rays per pixel (0 = off)
    int   giBounces       = 1;     // GI path bounces (1 = sky only; >1 = color bleed)
    float giStrength      = 1.0f;  // GI brightness multiplier
    float reflStrength    = 1.0f;  // global mirror-reflection multiplier
    float shininess       = 32.0f; // RT specular Phong exponent
    int   shadowSamples   = 1;     // soft-shadow rays per light (1 = hard)
    float shadowSoftness  = 0.05f; // penumbra radius
};
