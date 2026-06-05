<#
.SYNOPSIS
    Freeze the engine into this project (linked -> packaged mode).

    Reads EngineRoot.props next to this script, copies the engine sources
    from <EngineRootDir> into the project folder, then rewrites
    EngineRoot.props so the build uses the local copies instead.

    Re-running on an already-packaged project prints a message and exits.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$ProjectDir = $PSScriptRoot
$PropsPath  = Join-Path $ProjectDir 'EngineRoot.props'

Write-Host ''
Write-Host '  Package - freeze engine into this project' -ForegroundColor Cyan
Write-Host '  =========================================' -ForegroundColor Cyan
Write-Host ''

if (-not (Test-Path $PropsPath)) {
    Write-Host "  EngineRoot.props not found at:" -ForegroundColor Red
    Write-Host "    $PropsPath"                    -ForegroundColor Red
    Write-Host ''
    exit 1
}

# ── Read current EngineRootDir from the .props ────────────────────────────────
[xml]$xml = Get-Content $PropsPath -Raw -Encoding UTF8
$node = $xml.SelectSingleNode("//*[local-name()='EngineRootDir']")
if (-not $node) {
    Write-Host "  EngineRoot.props is malformed (no <EngineRootDir>)." -ForegroundColor Red
    exit 1
}
$current = $node.'#text'

# Already packaged? (literal "$(SolutionDir)")
if ($current -match '^\s*\$\(SolutionDir\)\s*$') {
    Write-Host '  Already packaged. Engine is local; nothing to do.' -ForegroundColor Green
    Write-Host ''
    exit 0
}

$EngineRoot = $current.TrimEnd('\','/')
if (-not (Test-Path $EngineRoot)) {
    Write-Host "  EngineRootDir points at a missing path:" -ForegroundColor Red
    Write-Host "    $EngineRoot"                            -ForegroundColor Red
    Write-Host '  Edit EngineRoot.props to point at the engine repo, or re-run GenerateProject.bat.' -ForegroundColor Yellow
    exit 1
}

Write-Host "  Engine source: $EngineRoot" -ForegroundColor DarkGray
Write-Host "  Project:       $ProjectDir" -ForegroundColor DarkGray
Write-Host ''

# ── Copy engine sources ───────────────────────────────────────────────────────
Write-Host '  Copying engine sources...' -ForegroundColor Yellow

$folders = @('Engine', 'include', 'lib')
foreach ($folder in $folders) {
    $src = Join-Path $EngineRoot $folder
    if (-not (Test-Path $src)) {
        Write-Host "    skip $folder\  (not found in engine repo)" -ForegroundColor DarkGray
        continue
    }
    Write-Host "    copy $folder\" -ForegroundColor DarkGray
    Copy-Item -Path $src -Destination (Join-Path $ProjectDir $folder) -Recurse -Force
}

# Runtime DLLs - refresh in case the engine added new ones since generation.
$binSrc = Join-Path $EngineRoot 'bin'
if (Test-Path $binSrc) {
    $binDst = Join-Path $ProjectDir 'bin'
    if (-not (Test-Path $binDst)) { New-Item -ItemType Directory -Path $binDst | Out-Null }
    Write-Host '    copy bin\*.dll' -ForegroundColor DarkGray
    Get-ChildItem -Path $binSrc -Filter '*.dll' -File |
        Copy-Item -Destination $binDst -Force
}

# OpenglViewer.props - the shared compile-flag sheet.
$propsSrc = Join-Path $EngineRoot 'OpenglViewer.props'
if (Test-Path $propsSrc) {
    Write-Host '    copy OpenglViewer.props' -ForegroundColor DarkGray
    Copy-Item -Path $propsSrc -Destination $ProjectDir -Force
}

# ── Rewrite EngineRoot.props (point at $(SolutionDir)) ────────────────────────
Write-Host '  Rewriting EngineRoot.props...' -ForegroundColor Yellow

$newProps = @'
<?xml version="1.0" encoding="utf-8"?>
<!--
    EngineRoot.props - packaged mode.
    Engine sources are copied alongside this project; the build resolves
    every $(EngineRootDir)... path through $(SolutionDir). Self-contained.
-->
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup Label="UserMacros">
    <EngineRootDir>$(SolutionDir)</EngineRootDir>
  </PropertyGroup>
  <ItemGroup>
    <BuildMacro Include="EngineRootDir">
      <Value>$(EngineRootDir)</Value>
    </BuildMacro>
  </ItemGroup>
</Project>
'@

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($PropsPath, $newProps, $utf8NoBom)

Write-Host ''
Write-Host '  Done. Project is now self-contained.' -ForegroundColor Green
Write-Host ''
