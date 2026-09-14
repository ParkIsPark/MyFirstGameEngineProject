[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
    Write-Host "PASS $Message"
}

function Write-Utf8([string]$Path, [string]$Text) {
    $parent = Split-Path -Parent $Path
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent | Out-Null
    }
    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$packageScript = Join-Path $repoRoot 'Template\Package.ps1'
$generatorScript = Join-Path $repoRoot 'Scripts\GenerateProject.ps1'
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("Lua packaging path with spaces " + [Guid]::NewGuid())
$projectDir = Join-Path $tempRoot 'Fixture Project'
$fakeEngine = Join-Path $tempRoot 'Minimal Engine Root'

try {
    New-Item -ItemType Directory -Path $projectDir | Out-Null
    Write-Utf8 (Join-Path $projectDir 'Content\Worlds\Saved.world') @'
WorldFormat = 2

[Actor]
Type = Actor
Name = Referenced
  [Component]
Type = ScriptComponent
Enabled = 1
Script = Content/Scripts/Referenced.lua
'@
    Write-Utf8 (Join-Path $projectDir 'Content\Scripts\Referenced.lua') 'referenced body'
    Write-Utf8 (Join-Path $projectDir 'Content\Scripts\Nested\Unreferenced.lua') 'unreferenced body'
    Write-Utf8 (Join-Path $projectDir 'Content\Outside.lua') 'outside body'
    Write-Utf8 (Join-Path $projectDir 'Content\Scripts\Ignore.txt') 'not lua'
    $externalScripts = Join-Path $tempRoot 'External Manifest Payload'
    Write-Utf8 (Join-Path $externalScripts 'Escaped.lua') 'must not package'
    $manifestLink = Join-Path $projectDir 'Content\Scripts\LinkedOutside'
    $mklinkCommand = "mklink /J `"$manifestLink`" `"$externalScripts`""
    & cmd.exe /d /c $mklinkCommand | Out-Null
    Assert-True ($LASTEXITCODE -eq 0 -and
                 ((Get-Item -LiteralPath $manifestLink -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) `
        'manifest fixture contains a real directory reparse point to an external Lua file'
    Assert-True ((Get-Content -LiteralPath $packageScript -Raw) -match 'ReparsePoint') `
        'package manifest policy explicitly rejects filesystem reparse entries'

    $manifestOutput = @(& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $packageScript `
        -ProjectDir $projectDir -ScriptManifestOnly)
    Assert-True ($LASTEXITCODE -eq 0) 'manifest dry-run succeeds from a project path containing spaces'
    $manifest = @($manifestOutput | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    Assert-True (($manifest -join "`n") -ceq "Content/Scripts/Nested/Unreferenced.lua`nContent/Scripts/Referenced.lua") `
        'dry-run manifest contains exactly sorted in-root Lua files and excludes external reparses'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $projectDir 'ScriptManifest.txt'))) `
        'manifest-only mode is non-mutating'
    Assert-True ((Get-Content -LiteralPath (Join-Path $projectDir 'Content\Scripts\Referenced.lua') -Raw) -ceq 'referenced body' -and
                 (Get-Content -LiteralPath (Join-Path $projectDir 'Content\Scripts\Nested\Unreferenced.lua') -Raw) -ceq 'unreferenced body') `
        'manifest discovery preserves original script contents and relative locations'

    foreach ($folder in @('Engine', 'include', 'lib', 'bin', 'ThirdParty\Lua\5.4.9\src')) {
        New-Item -ItemType Directory -Path (Join-Path $fakeEngine $folder) | Out-Null
    }
    Write-Utf8 (Join-Path $fakeEngine 'Engine\Marker.h') 'engine marker'
    Write-Utf8 (Join-Path $fakeEngine 'Config\DeveloperSettings.ini') "[Rendering]`nLegacyOverride=PureGPURayTracer`nShowDeprecatedFeatures=false`nShowExperimentalWarnings=false"
    Write-Utf8 (Join-Path $fakeEngine 'ThirdParty\Lua\5.4.9\src\lua.h') 'vendored lua marker'
    Write-Utf8 (Join-Path $fakeEngine 'OpenglViewer.props') '<Project />'
    $engineForProps = $fakeEngine
    if (-not $engineForProps.EndsWith('\')) { $engineForProps += '\' }
    Write-Utf8 (Join-Path $projectDir 'EngineRoot.props') @"
<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup><EngineRootDir>$engineForProps</EngineRootDir></PropertyGroup>
</Project>
"@
    $regularOutput = @(& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $packageScript -ProjectDir $projectDir)
    Assert-True ($LASTEXITCODE -eq 0) 'regular package freeze succeeds against a minimal engine root with spaces'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $projectDir 'Config\DeveloperSettings.ini')) -and
                 -not (Test-Path -LiteralPath (Join-Path $projectDir 'bin\DeveloperSettings.ini'))) `
        'package freeze never copies local Developer Settings from the engine into project or bin'
    Assert-True ((Get-Content -LiteralPath (Join-Path $projectDir 'ThirdParty\Lua\5.4.9\src\lua.h') -Raw) -ceq
                 'vendored lua marker') `
        'regular package copies the vendored Lua build dependency into the self-contained project'
    $writtenManifest = @(Get-Content -LiteralPath (Join-Path $projectDir 'ScriptManifest.txt'))
    Assert-True (($writtenManifest -join "`n") -ceq ($manifest -join "`n")) `
        'regular package writes the same reparse-safe deterministic manifest as dry-run'
    Assert-True ((Test-Path -LiteralPath (Join-Path $projectDir 'Content\Scripts\Referenced.lua')) -and
                 (Test-Path -LiteralPath (Join-Path $projectDir 'Content\Scripts\Nested\Unreferenced.lua')) -and
                 (Get-Content -LiteralPath (Join-Path $projectDir 'Content\Outside.lua') -Raw) -ceq 'outside body') `
        'regular freeze retains all project-owned Content without rebasing or deletion'
    Assert-True ((Get-Content -LiteralPath (Join-Path $externalScripts 'Escaped.lua') -Raw) -ceq 'must not package') `
        'regular package leaves the external reparse target untouched'

    Write-Utf8 (Join-Path $projectDir 'Content\Scripts\Nested\AddedAfterFreeze.lua') 'late body'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $packageScript -ProjectDir $projectDir | Out-Null
    $refreshedManifest = @(Get-Content -LiteralPath (Join-Path $projectDir 'ScriptManifest.txt'))
    Assert-True ($LASTEXITCODE -eq 0 -and ($refreshedManifest -join "`n") -ceq
                 "Content/Scripts/Nested/AddedAfterFreeze.lua`nContent/Scripts/Nested/Unreferenced.lua`nContent/Scripts/Referenced.lua") `
        're-running an already frozen package refreshes the manifest without touching Content'

    $generatedParent = Join-Path $tempRoot 'Generated Parent With Spaces'
    New-Item -ItemType Directory -Path $generatedParent | Out-Null
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $generatorScript -Parent $generatedParent -Name 'GeneratedLua'
    Assert-True ($LASTEXITCODE -eq 0) 'project generation succeeds beneath a path containing spaces'
    $generated = Join-Path $generatedParent 'GeneratedLua'
    $templateExample = Join-Path $repoRoot 'Template\Content\Scripts\ExampleActor.lua'
    $generatedExample = Join-Path $generated 'Content\Scripts\ExampleActor.lua'
    Assert-True ((Test-Path -LiteralPath $generatedExample) -and
                 (Get-Content -LiteralPath $generatedExample -Raw) -ceq (Get-Content -LiteralPath $templateExample -Raw)) `
        'generated projects contain the byte-identical template ExampleActor script'
    [xml]$generatedProject = Get-Content -LiteralPath (Join-Path $generated 'GeneratedLua.vcxproj') -Raw
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $generated 'Config\DeveloperSettings.ini')) -and
                 -not (Test-Path -LiteralPath (Join-Path $generated 'bin\DeveloperSettings.ini'))) `
        'project generation excludes local Developer Settings'
    $visibleLua = @($generatedProject.Project.ItemGroup.None | Where-Object { $_.Include -eq 'Content\Scripts\**\*.lua' })
    Assert-True ($visibleLua.Count -eq 1) 'generated project files expose nested Lua content in Solution Explorer'
    $generatedManifest = @(& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $generated 'Package.ps1') `
        -ScriptManifestOnly)
    Assert-True ($LASTEXITCODE -eq 0 -and ($generatedManifest -join "`n") -ceq 'Content/Scripts/ExampleActor.lua') `
        'generated project package dry-run discovers its example through the same manifest policy'

    [xml]$engineProject = Get-Content -LiteralPath (Join-Path $repoRoot 'Engine.vcxproj') -Raw
    $rootVisibleLua = @($engineProject.Project.ItemGroup.None | Where-Object { $_.Include -eq 'Content\Scripts\**\*.lua' })
    Assert-True ($rootVisibleLua.Count -eq 1) 'engine project exposes its root example Lua content in Solution Explorer'
}
finally {
    if (Test-Path -LiteralPath $tempRoot) { Remove-Item -LiteralPath $tempRoot -Recurse -Force }
}

Write-Host '=== script packaging: all checks passed ==='
