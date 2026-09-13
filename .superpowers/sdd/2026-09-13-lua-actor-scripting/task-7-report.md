# Task 7 Report: Editor Assignment and Lua Packaging

## Outcome

Implemented Task 7 on reviewed base `f36a8cb` without expanding the Lua gameplay API or edit-mode execution surface.

- The Details panel lists and edits every ScriptComponent by stable actor component index. It supports add, enabled toggle, assign/change by drag, discovered-asset picker, native `.lua` import, clear, and exact removal. Existing PIE disabling makes every mutation control read-only while playing.
- External Lua choices are validated before component mutation and copied without overwrite beneath `Content/Scripts`; in-root nested choices retain their project-relative path. Rejected choices retain the prior assignment and show a readable message.
- `ClearScriptPath()` preserves an unassigned component. Saving omits `Script`; loading an omitted field clears the factory-created component; explicit invalid fields retain the existing atomic-validation contract.
- `AActor::RemoveNonSpatialComponent` follows the actor mutation guard, rejects null/foreign/not-owned/spatial/typed-alias components, destroys exactly the requested attachment, and preserves survivor order.
- Content discovery is recursive and deterministic, stores content-relative subpaths, classifies `.lua` case-insensitively, and preserves full nested paths for existing open/import/material/rename/delete workflows. Nested world save locations and dotted world names are preserved.
- `Package.ps1 -ScriptManifestOnly` is non-mutating and emits every regular `.lua` beneath `Content/Scripts`, including unreferenced files, as ordinally sorted forward-slash project-relative lines. Normal packaging writes and reports the same `ScriptManifest.txt`, including on already-frozen reruns, without deleting or rebasing project-owned Content.
- Generated projects receive the template `ExampleActor.lua` byte-for-byte and expose `Content\Scripts\**\*.lua` in Solution Explorer. The example uses BeginPlay, Tick, EndPlay, and `Engine.Log` without implying an Actor API.

## RED evidence

Tests were added before their corresponding production changes.

1. Initial focused run:
   - `powershell Test/ScriptPackagingTest.ps1`
   - Failure: `Package.ps1 : A parameter cannot be found that matches parameter name 'ProjectDir'.`
   - Focused MSVC compile of `Test/ScriptEditorWorkflowTest.cpp`
   - Failure: `fatal error C1083: Cannot open include file: 'FEditorAssetWorkflow.h'`.
2. Nested world identity test added before its helper:
   - Focused compile failed with `error C3861: 'EditorWorldNameFromPath': identifier not found`.
3. Already-frozen manifest-refresh assertion added before moving manifest generation ahead of the freeze early return:
   - `Test/ScriptPackagingTest.ps1` failed `re-running an already frozen package refreshes the manifest without touching Content`.
4. Self-review found that recursive world loading could retain a nested name but Save created only the Content root. A focused test was added first:
   - Focused compile failed at line 106 with `error C3861: 'PrepareEditorWorldSavePath': identifier not found`.
   - After the first implementation, the dotted-name mutation check failed with exactly one failure: `FAIL nested world save preparation creates its preserved parent path without changing dotted names`.
5. Independent review identified physical containment gaps in editor import:
   - A junction-backed `Content/Scripts` test failed with `FAIL assignment rejects a reparse-pointed Scripts directory before copying outside Content`; the original helper copied the file into the external junction target.
   - After adding canonical project/Content/Scripts containment, a second linked-Content test failed with `FAIL assignment validates a reparse-pointed Content root before creating Scripts outside the project`; the helper rejected assignment but had already created `Scripts` in the external Content target.
   - The final order validates the physical Content root before any directory mutation, then creates/resolves Scripts and validates its physical containment before any file copy.
6. Root solution visibility assertion was added before the engine project entry:
   - Packaging checks failed only at `FAIL: engine project exposes its root example Lua content in Solution Explorer`.

The PowerShell GREEN path also exposed two environment-specific harness faults before product assertions could run: Windows PowerShell 5.1 has no `System.IO.Path.GetRelativePath`, and `$PSScriptRoot` was empty when used as a parameter default. The implementation now uses a PS 5.1-compatible substring relative path and assigns the default project root after the parameter block.

## GREEN evidence

Fresh final verification from the Task 7 worktree:

- `MSBuild.exe Engine.sln /m /t:Build /p:Configuration=Debug /p:Platform=Win32 /nologo /v:minimal`
  - Exit 0; `Engine.vcxproj -> bin\Engine.lib`, `Test.vcxproj -> bin\Test.exe`.
- Focused MSVC build/run of `Test/ScriptEditorWorkflowTest.cpp` against `bin\Engine.lib`
  - 15 PASS, `=== script editor workflow: 0 failed ===`, including real Windows junction escape coverage for both Content and Content/Scripts.
- Focused Task 5 MSVC build/run of `Test/ScriptComponentSerializationTest.cpp`
  - `10 passed, 0 failed`.
- Focused Task 6 MSVC build/run of `Test/ScriptLifecycleTest.cpp`
  - 18 PASS, exit 0.
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Test\ScriptPackagingTest.ps1`
  - All 13 packaging/generation assertions passed, including a fixture path containing spaces, referenced plus nested-unreferenced policy, non-mutating dry-run, regular fake-engine freeze, retained Content, already-frozen refresh, generated example equality, template/root project visibility, and generated manifest discovery.
- Fresh generated project beneath `%TEMP%\LuaT7 final path with spaces <guid>`:
  - `Editor|Win32` build exit 0.
  - `Game|Win32` build exit 0.
  - Generated `Package.ps1 -ScriptManifestOnly` returned exactly `Content/Scripts/ExampleActor.lua`.
- All modified `.vcxproj` and `.filters` files parsed as XML; `git diff --check` reported no whitespace errors.

Build output retains existing repository warnings (notably code-page C4819, GLM deprecation, and ray-tracer narrowing warnings). The temporary generated build also reports MSB8029 because its intermediates intentionally live beneath `%TEMP%`; neither configuration produced errors.

## Self-review

- Re-read the Task 7 brief line by line and confirmed the change remains limited to editor authoring, safe component removal, recursive content paths, serialization omission, packaging/generation, example content, project entries, and focused tests.
- Confirmed no script instance is created by discovery, assignment, clear, save, tick of a non-playing world, removal, or packaging; the focused edit-world check observes no Lua log output.
- Confirmed recursive discovery consumers reconstruct paths through the common helper and nested rename retains the original parent rather than flattening.
- Confirmed typed mesh/physics and all scene-component aliases cannot pass the new removal boundary.
- Independent review found no Critical issue. Its Important reparse-point containment finding was reproduced twice with focused REDs and fixed before commit; its two minor UI/project-visibility findings were also applied.
- GUI interaction is not automated in this headless workflow. The real ImGui controls compile in Engine, Test, and generated Editor configurations, while their GL-free state/path behavior is covered by the focused helper test.
- A direct non-MSVC/UCRT experiment was not used as acceptance evidence because the reviewed base has a pre-existing GCC-incompatible default argument in `UFbxImporter.h:28`. The required Visual Studio Win32 route above is clean at the error level.

No product-behavior ambiguity or Task 8 scope expansion was required.

## Controller fix round 1 (`dcbea6f`)

All six Important controller findings were reproduced or reduced to a failing
link/policy assertion before the corresponding production edit.

### RED evidence

1. The first focused MSVC run after adding the path-boundary assertions reported
   exactly three failures:
   - `FAIL content discovery and resolution reject lexical escapes and external file reparses`
   - `FAIL assignment rejects a Lua-named reparse whose resolved target is not Lua`
   - `FAIL failed component assignment rolls back an external Lua copy`
   The host does not grant `SeCreateSymbolicLinkPrivilege` (both
   `std::filesystem::create_symlink` and `cmd mklink` reported insufficient
   privilege), so the final test automatically uses a real directory junction as
   the equivalent reparse fixture. The same test will exercise a file symlink
   when the host permits creating one.
2. After adding tests for the editor's desired narrow APIs, the focused link failed
   with four unresolved production symbols: `CopyEditorAssetToContent`,
   `RenameEditorContentAsset`, `DeleteEditorContentAsset`, and
   `SelectEditorScriptAsset`. Those assertions cover the nested-name collision,
   external reparse rename/delete protection, safe nested mutation, and a picker
   selection that remains valid after its model is cleared.
3. `Test/ScriptPackagingTest.ps1` created a real junction from
   `Content/Scripts/LinkedOutside` to an external directory containing
   `Escaped.lua`, then failed at
   `FAIL: package manifest policy explicitly rejects filesystem reparse entries`.
4. The initial required UCRT64 attempt exposed test-command defects in sequence:
   PowerShell parsed the unquoted `-Wl,--gc-sections`, the include list lacked
   `Engine/RayTracing`, and the over-coupled helper TU linked against
   `UScriptComponent::SetScriptPath`. The filesystem operations were then kept in
   `FEditorAssetWorkflow.cpp` while component assignment moved to
   `FEditorScriptWorkflow.cpp`; this provides an exact two-input UCRT64 command
   (test plus its one production TU) at the top of the standalone test rather
   than silently substituting MSVC.
5. Final self-review added a root-asset rename assertion to protect existing
   Content Browser behavior. The UCRT64 run reproduced exactly
   `FAIL rename preserves existing root-level content behavior`; the physical
   parent check incorrectly required a strict descendant. Allowing the safe
   Content root as the destination parent made the same run green while retaining
   strict containment for files.

### GREEN evidence

Fresh verification after all fixes:

- Exact command recorded at the top of `Test/ScriptEditorWorkflowTest.cpp`:
  `C:\msys64\ucrt64\bin\g++.exe ... Test\ScriptEditorWorkflowTest.cpp Engine\Editor\FEditorAssetWorkflow.cpp ...`
  compiled and ran successfully: 10 PASS, `0 failed`. This directly covers
  recursive lexical identity, real-junction discovery/resolve/rename/delete
  rejection, safe nested mutation, non-flattening copy, nested world identity,
  and stable picker selection.
- Full focused MSVC workflow: 23 PASS, `0 failed`. It additionally covers
  multiple exact ScriptComponents, external assignment, reparse rejection,
  rollback after an undo/component-change failure, omitted unassigned
  serialization, alias-safe removal, and no Lua work during editing.
- `Test/ScriptPackagingTest.ps1`: all 16 assertions passed. Both dry-run and
  regular freeze exclude the external reparse, retain its target unchanged, and
  retain the referenced plus nested-unreferenced scripts under a path with spaces.
- `Engine.sln` `Debug|Win32`: exit 0; both `Engine.lib` and `Test.exe` built.
- Task 5 serialization regression: 10 passed, 0 failed.
- Task 6 real-Lua lifecycle regression: all 18 checks passed.
- A freshly generated project under `%TEMP%\LuaT7 Final Path` built both
  `Editor|Win32` and `Game|Win32`; its separate `Editor` and `Game` intermediate
  directories were produced and the final Game executable was 626,176 bytes.
  Its manifest was exactly `Content/Scripts/ExampleActor.lua`. The temporary
  fixture was removed after verification.
- An intentionally longer first fixture under the already-long worktree hit a
  Windows compiler-generated-file `C1083` path-length error. Repeating under the
  shorter `%TEMP%` space-containing path passed; this did not require a product
  change.

### Fix details and self-review

- Content identity now comes from `lexically_relative`, rejects empty/rooted/dot
  traversal, rejects reparse components, and confirms canonical physical
  containment before returning a mutable path. The real Content Browser routes
  rename and delete through these checked helpers.
- `EditorEngine::CopyToContent` routes its primary asset through the tested helper.
  Any safe descendant already beneath Content returns its complete nested path;
  it is never flattened onto an unrelated root filename. Existing external OBJ
  sidecar behavior remains intact.
- Script assignment rejects every source path containing a file or directory
  reparse, validates the resolved source extension, then normalizes and validates
  the exact `Content/Scripts/*.lua` serialized identity before copying. A copy is
  removed if the undo hook or `SetScriptPath` fails.
- The picker stores a path value while iterating and invokes assignment only after
  `EndPopup`, so the successful assignment refresh cannot invalidate its active
  vector reference.
- Package manifest enumeration rejects a reparse at Scripts, at the file, or at
  any intervening path component. It still includes every ordinary in-root Lua
  file, referenced or not.
- All engine, Test, and Template project/filter pairs include the split production
  unit and parse/build successfully. No gameplay binding, hot reload, renderer,
  or Task 8 behavior was added.
- Headless automation now covers the exact production path/model operations,
  world save/load and no-edit-execution contract, real Lua Play lifecycle,
  generation, regular package freeze, both generated build configurations, and
  manifest output. The only manual-only remainder is visual ImGui click-through
  (drag/picker/native dialog/clear/remove and observing the standalone window);
  those real controls compile, but a fake ImGui oracle was deliberately not
  introduced.

This section supersedes the earlier statement that UCRT64 was not acceptance
evidence: the final focused command is complete and was actually run.
