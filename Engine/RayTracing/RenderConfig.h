#pragma once

// =============================================================================
//  RenderConfig.h  —  compile-time rendering constants
//
//  These values are baked into the GPU shader at Init() time and used as
//  defaults for the CPU RenderSettings struct.  Change them and rebuild to
//  switch behaviour without touching scene or pipeline code.
// =============================================================================

// ── Anti-aliasing ─────────────────────────────────────────────────────────────
// AA_ENABLE   : 0 = off (single centre sample), 1 = on
// AA_GRID     : grid dimension; total samples = AA_GRID * AA_GRID
//               Examples: 1=1spp  2=4spp  3=9spp  4=16spp
// AA_MODE     : sampling pattern
//               1 = Random jitter          – fast, noisy
//               2 = Stratified jitter      – recommended (uniform + random)
//               3 = Halton sequence        – best quality, deterministic
#define AA_ENABLE  1              // 0=off; 1=on
#define AA_GRID    2              // total samples = AA_GRID^2  (2→4spp, 3→9spp, 4→16spp)
#define AA_MODE    2

// ── Gamma correction ──────────────────────────────────────────────────────────
// GAMMA_ENABLE : 0 = linear output, 1 = apply gamma
// GAMMA_VALUE  : 1.8 (macOS legacy) / 2.2 (sRGB / Windows) / 2.4 (BT.1886)
#define GAMMA_ENABLE  1
#define GAMMA_VALUE   2.2f
