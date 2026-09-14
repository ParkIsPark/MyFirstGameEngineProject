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
    int   giSamples       = 4;     // hemisphere GI rays per pixel (0 = off)
    int   giBounces       = 1;     // 0 = AO/environment visibility; 1..4 = iterative diffuse bounces
    float giStrength      = 1.0f;  // GI brightness multiplier
    float reflStrength    = 1.0f;  // global mirror-reflection multiplier
    float shininess       = 32.0f; // RT specular Phong exponent
    int   shadowSamples   = 4;     // soft-shadow rays per light (1 = hard)
    float shadowSoftness  = 0.05f; // penumbra radius
    float exposureEV      = 0.0f;  // presentation exposure in stops
    int   temporalFrames  = 32;    // maximum temporal accumulation frames
    float anisotropy      = 8.0f;  // requested texture anisotropy
    bool  depthView       = false; // editor-only visualization, explicit per request
};
