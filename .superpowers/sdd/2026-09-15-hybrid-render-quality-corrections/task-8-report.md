# Task 8 Report: sRGB Mipmaps and Capability-Bounded Anisotropy

## Status

Implemented and verified on the available OpenGL 4.6 context, exercising the
Compatible GL3.3 submission path and the optional GL4.3 compute path.

## RED / GREEN

- RED policy: `TextureSamplingPolicyTest` compiled and failed to link on the
  missing `EffectiveAnisotropy` and `ResampleRGBA8ToLayer` implementations.
- GREEN policy: unsupported and malformed capabilities fall back to 1x;
  requested 8x is bounded by 16x/4x maxima; full-layer resampling fills all
  target texels.
- RED integration: raster lighting reported 2 expected failures (linear
  base-level storage and revision-only refresh); ray effects reported 4
  expected failures (padded atlas, revision-only repack, GL3.3 and GL4.3
  texture storage/filtering).
- GREEN integration: raster 69/69 and ray 183/183, including sRGB formats,
  complete mip chains, trilinear minification, bounded anisotropy, normalized
  full-layer UV sampling, rollback, and hostile unpack/PBO/texture/sampler
  state restoration.

## Implementation

- Added the shared texture sampling policy and CPU RGBA8 full-layer resampler.
- Cached anisotropy support and maximum once per context generation; malformed
  or absent capability data silently produces the 1x/trilinear fallback.
- Raster base-color uploads now use `GL_SRGB8_ALPHA8`, generated mipmaps,
  trilinear minification, and bounded requested anisotropy. Manual shader
  gamma decode was removed.
- Ray atlas packing now resamples every source across a complete common-size
  layer with no black unused area. Reserved texel 5 remains present, retaining
  exactly eight texels per material; both ray shaders sample normalized layer
  UV directly.
- GL3.3 and GL4.3 ray arrays use the same sRGB/mipmap/anisotropy policy.
- Raster and ray invalidation include the runtime-only material revision,
  without changing serialization or Task 7 history semantics.
- Added the new engine files to Engine, Test, and Template projects and filters.

## Verification

- `MSBuild Engine.sln Debug|Win32 /m:1 /nr:false`: success.
- `RunStandaloneTests.ps1`: 35 sources, 0 failures.
- `TextureSamplingPolicyTest`: 1 source, 0 failures.
- `--raster-lighting-selftest`: 69 passed, 0 failed.
- `--ray-effects-selftest`: 183 passed, 0 failed.
- `--render-performance-selftest`: 389 passed, 0 failed; production
  Editor/Game hooks also passed.
- `git diff --check`: clean (line-ending conversion notices only).
- Forbidden manual texture gamma and logical-subrect shader searches: no hits.
- Generated `task13_final_upload.tmp.hdr`: absent. User-owned `Test/Config/`
  remains untracked and unstaged.

## Self-review

- Capability probing happens before upload operations, so its local error
  validation cannot consume a later allocation/upload error.
- Existing candidate-object rollback and committed-resource swaps remain in
  place around mip generation and sampler configuration.
- GL3.3 retains its 16-unit baseline; no sampler or material-record expansion.
- The 256 MiB pre-allocation guard still bounds the complete ray array.

## Concerns

- The available machine exposes OpenGL 4.6; the GL3.3 backend and its strict
  baseline rules were exercised, but not on a physical 3.3-only driver.
- Existing repository C4819 and deprecated-GLM build warnings remain unchanged.
