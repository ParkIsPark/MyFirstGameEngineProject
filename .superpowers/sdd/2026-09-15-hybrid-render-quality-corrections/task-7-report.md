# Task 7 Report: Temporal Sequencing and Reconstruction

## Status

Implemented shared OpenGL 3.3-compatible temporal reconstruction for both ray backends. The renderer now varies every stochastic ray hash by `frameIndex`, accumulates shadowed-direct and GI in independent ping-pong `RGBA16F` histories, runs edge-aware spatial filtering, resets deterministically on the complete render-history signature, and forwards current raw outputs if reconstruction fails.

## RED / GREEN

- RED: `RenderHistoryTest` initially failed to compile because `FRenderHistory.h` did not exist. This established the new public history API boundary before production code.
- GREEN: `RenderHistoryTest` passes and covers stable advancement plus camera, projection, transform, mesh geometry revision, resolved material scalar, monotonic `runtimeRevision`, texture path/timestamp, light, environment, feature, quality, size, backend, context, and explicit reset invalidation.
- GL acceptance probes exercise 32-frame static convergence, camera-reset frame index zero, byte-identical no-ghost output versus a fresh renderer, bounded resize ownership, allocation/release balance, and 100 unchanged steady-state frames with zero reconstruction texture/framebuffer allocation.

## Architecture

- `FRenderHistory` uses exact IEEE float bits and explicit scalar/string fields. It never hashes texture byte arrays. Material texture paths are paired with filesystem last-write stamps, and `FResolvedRenderMaterial::runtimeRevision` is included even when path/stamp are unchanged.
- `URasterLightingPass::EnvironmentRevision()` is monotonic and changes only when the effective HDRI/procedural resource changes; environment scalar settings and path/stamp are independently in the signature.
- `UWorldRenderer` computes the temporal frame before backend evaluation, supplies `frameIndex` to both shaders, reconstructs successful raw outputs before HDR composition, and resets both sequence and reconstruction on backend failure, disabled/depth routes, shutdown, and signature changes.
- Reconstruction owns four temporal history textures, four disjoint scratch textures, one FBO, two programs, and one fullscreen VAO. Resize allocation is transactional and steady frames allocate nothing.
- Shadowed direct uses two separable bilateral passes at steps 1 and 2. GI uses A-trous passes at steps 1, 2, and 4. Guides reject object/material identity changes, depth deltas over `max(0.01, 0.02 * abs(centerDepth))`, and normal dots below `0.8`; accepted normal weights are `pow(dot, 32)`.
- Optical contribution is passed through unchanged and is never spatially blurred.
- The fullscreen state guard preserves independent read/draw FBOs, draw buffers, viewport, program/VAO, texture/sampler bindings, unpack state, indexed blend/color masks, and all modified fixed-function state.

## Files

- Added: `Engine/Render/FRenderHistory.h/.cpp`
- Added: `Engine/Render/URayEffectsReconstruction.h/.cpp`
- Added: `Engine/Render/Shaders/RayEffectsReconstructionShaders.h`
- Added: `Test/RenderHistoryTest.cpp`
- Modified: ray input/backend shaders, raster environment revision, world renderer integration/stats, `Test/main.cpp`, and all six Visual Studio project/filter files.
- Preserved: untracked `Test/Config/`.

## Verification

- `MSBuild Engine.sln /m:1 /nr:false /p:Configuration=Debug /p:Platform=Win32`: success, zero errors (four pre-existing code-page warnings in the final incremental build).
- `RunStandaloneTests.ps1`: 34 sources, 0 failures.
- `Test.exe --ray-effects-selftest=...`: 180 passed, 0 failed; GL 3.3 and capability-gated GL 4.3 exercised.
- `Test.exe --render-performance-selftest`: 389 passed, 0 failed; includes the 100-frame zero-allocation gate.
- `git diff --check`: clean apart from Git's existing LF-to-CRLF notices.

## Self-review / Concerns

- Corrected a capped-index ping-pong hazard: `frameIndex` continues monotonically while only the running-average weight is capped.
- Corrected cross-effect scratch aliasing by giving shadow and GI disjoint filter scratch pairs.
- Background identity is handled explicitly to prevent undefined normalization from contaminating empty pixels.
- No known functional blockers. The build continues to emit repository-existing CP949/code-page warnings.
