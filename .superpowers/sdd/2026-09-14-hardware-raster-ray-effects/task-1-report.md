# Task 1 Implementation Report

## Status

DONE

## What I implemented

- Added the GL-free render-feature vocabulary in `Engine/Render/FRenderFeatures.h`:
  - `ERayTracingBackend`
  - `ELegacyRendererOverride`
  - `FRenderFeatures`, including mandatory-by-default `hardwareRaster`
- Added the pass vocabulary and pure planner declaration in `Engine/Render/FRenderPipelinePlan.h`.
- Added a deterministic, side-effect-free planner in `Engine/Render/FRenderPipelinePlan.cpp`.
  - Every plan begins with `HardwareGBuffer`, followed by `RasterLighting`.
  - Exactly one `RayTracedEffects` pass is added only when the master switch and at least one child effect are enabled.
  - Every plan ends with `Composite`.
  - `hardwareRaster=false` and backend selection do not alter pass ordering.
  - Each call constructs and returns a fresh `std::vector`.
- Added a standalone behavior test in `Test/RenderPipelinePlanTest.cpp`. Its first line contains the complete MSYS2 UCRT64 compile/run command and links `FRenderPipelinePlan.cpp`.
- Added the new production source and headers to all three project/filter pairs under the existing `Engine\Render` filter conventions. The standalone test itself was intentionally not added to `Test.vcxproj`, per repository convention.
- Did not change runtime renderer routing, `URenderer::ERenderMode`, `UScene::renderMode`, editor UI, serialization, OpenGL resources/calls, Lua, or legacy execution behavior.

## TDD evidence

### RED

Command run from the repository root before any production files existed:

```powershell
& 'C:\msys64\ucrt64\bin\g++.exe' -std=c++17 -Wall -Wextra -pedantic -I./Engine/Render Test/RenderPipelinePlanTest.cpp Engine/Render/FRenderPipelinePlan.cpp -o RenderPipelinePlanTest.exe; if ($LASTEXITCODE -eq 0) { .\RenderPipelinePlanTest.exe }; exit $LASTEXITCODE
```

Exit code: `1`.

Relevant expected output:

```text
Test/RenderPipelinePlanTest.cpp:2:10: fatal error: FRenderPipelinePlan.h: No such file or directory
cc1plus.exe: fatal error: Engine/Render/FRenderPipelinePlan.cpp: No such file or directory
```

This was the expected RED because the test compiled against the desired public API before the header, declaration, or implementation existed.

### GREEN

Same compile/run command after the minimal implementation:

```powershell
& 'C:\msys64\ucrt64\bin\g++.exe' -std=c++17 -Wall -Wextra -pedantic -I./Engine/Render Test/RenderPipelinePlanTest.cpp Engine/Render/FRenderPipelinePlan.cpp -o RenderPipelinePlanTest.exe; if ($LASTEXITCODE -eq 0) { .\RenderPipelinePlanTest.exe }
```

Exit code: `0`.

Output:

```text
RenderPipelinePlanTest passed
```

Final focused run after self-review added explicit coverage for `hardwareRaster=false` also exited `0` with the same pristine output. The generated executable was removed after each successful verification.

## Test coverage

- Exact default/raster-only sequence.
- Mandatory hardware-raster primary visibility even if the vocabulary field is locally set false.
- RT master switch disabled while all child defaults remain enabled.
- RT master switch enabled with all child effects disabled.
- Shadows only, GI only, reflections only, and all effects together.
- Exactly one ray-effects pass immediately before composite.
- Auto, CompatibleGL33, and ComputeGL43 backend choices all preserve ordering.
- Repeated calls are deterministic and return independent values.

## Integration verification

### XML parsing

Parsed all six modified XML files with PowerShell's XML parser. Each emitted `XML PASS`:

- `Engine.vcxproj`
- `Engine.vcxproj.filters`
- `Test/Test.vcxproj`
- `Test/Test.vcxproj.filters`
- `Template/Template.vcxproj`
- `Template/Template.vcxproj.filters`

### Lua/project topology

Command:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Test\ScriptPackagingTest.ps1
```

Exit code: `0`.

Summary output:

```text
=== script packaging: all checks passed ===
```

The project-file diff contains only additive entries for the three new render files; all existing Lua compile/include/content entries and include paths are unchanged.

### Engine solution

Command:

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
& $msbuild Engine.sln /m /p:Configuration=Debug /p:Platform=Win32 /clp:ErrorsOnly
```

The sandboxed attempt could not read installed Windows SDK metadata under the user profile. The same required command was rerun with approved external SDK access and exited `0`; `ErrorsOnly` output contained no errors.

### Generated Template project

Generated a temporary linked project using `Scripts/GenerateProject.ps1`, then built its solution in both configurations:

- `Editor|Win32`: exit code `0`, no errors.
- `Game|Win32`: exit code `0`, no errors.

The generated project contained `$(EngineRootDir)Engine\Render\FRenderPipelinePlan.cpp` and therefore compiled the new source in both configurations. The temporary generated project was removed after verification.

### Diff hygiene

`git diff --check` exited `0`. No standalone test executable or object remains in the repository root.

## Files changed

- `.superpowers/sdd/2026-09-14-hardware-raster-ray-effects/task-1-report.md`
- `Engine/Render/FRenderFeatures.h`
- `Engine/Render/FRenderPipelinePlan.h`
- `Engine/Render/FRenderPipelinePlan.cpp`
- `Test/RenderPipelinePlanTest.cpp`
- `Engine.vcxproj`
- `Engine.vcxproj.filters`
- `Test/Test.vcxproj`
- `Test/Test.vcxproj.filters`
- `Template/Template.vcxproj`
- `Template/Template.vcxproj.filters`

## Self-review

- Completeness: compared the implementation and tests against every behavior and integration bullet in the corrected task brief. All required vocabulary, ordering cases, backend invariance, fresh-value behavior, project topology, and verification targets are covered.
- Quality: the public vocabulary is split from the planner contract, and the implementation has no dependencies beyond the feature object and `std::vector`.
- Scope discipline: no runtime migration or GL-facing code was introduced. Existing renderer modes, saved settings, UI, Lua entries, and serialization remain untouched for later tasks.
- Mutation check: the tests fail if the primary passes are reordered/omitted, the composite pass is misplaced, the RT master or child mask is ignored, more than one ray pass is inserted, backend changes ordering, `hardwareRaster=false` suppresses normal primary visibility, or a mutable/shared plan is returned.
- Findings fixed during self-review: added an explicit `hardwareRaster=false` case so the mandatory-primary-visibility contract is protected rather than only implied by the default case.

## Concerns

None.
