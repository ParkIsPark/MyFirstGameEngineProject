<#
.SYNOPSIS
    Freeze the engine into this project (linked -> packaged mode).

    Reads EngineRoot.props next to this script, copies the engine sources
    from <EngineRootDir> into the project folder, then rewrites
    EngineRoot.props so the build uses the local copies instead.

    Re-running on an already-packaged project prints a message and exits.
#>

[CmdletBinding()]
param(
    [string]$ProjectDir = '',
    [switch]$ScriptManifestOnly
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) { $ProjectDir = $PSScriptRoot }
$ProjectDir = [System.IO.Path]::GetFullPath($ProjectDir)
$PropsPath  = Join-Path $ProjectDir 'EngineRoot.props'

function Test-PathContainsReparsePoint([string]$Path, [string]$StopRoot) {
    $current = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $stop = [System.IO.Path]::GetFullPath($StopRoot).TrimEnd('\', '/')
    while ($true) {
        if (-not $current.StartsWith($stop, [StringComparison]::OrdinalIgnoreCase)) { return $true }
        $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { return $true }
        if ($current.Equals($stop, [StringComparison]::OrdinalIgnoreCase)) { return $false }
        $parent = [System.IO.Directory]::GetParent($current)
        if ($null -eq $parent) { return $true }
        $current = $parent.FullName.TrimEnd('\', '/')
    }
}

function Get-ProjectScriptManifest([string]$Root) {
    $items = [System.Collections.Generic.List[string]]::new()
    $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $scriptsRoot = Join-Path $Root 'Content\Scripts'
    if ((Test-Path -LiteralPath $scriptsRoot -PathType Container) -and
        -not (Test-PathContainsReparsePoint $scriptsRoot $rootFull)) {
        foreach ($file in Get-ChildItem -LiteralPath $scriptsRoot -Recurse -File) {
            if ($file.Extension -ine '.lua') { continue }
            if (Test-PathContainsReparsePoint $file.FullName $scriptsRoot) { continue }
            $fileFull = [System.IO.Path]::GetFullPath($file.FullName)
            $relative = $fileFull.Substring($rootFull.Length).TrimStart('\', '/').Replace('\', '/')
            $items.Add($relative)
        }
    }
    $manifest = $items.ToArray()
    [Array]::Sort($manifest, [StringComparer]::Ordinal)
    return $manifest
}

$scriptManifest = @(Get-ProjectScriptManifest $ProjectDir)
if ($ScriptManifestOnly) {
    $scriptManifest | Write-Output
    return
}

Write-Host ''
Write-Host '  Package - freeze engine into this project' -ForegroundColor Cyan
Write-Host '  =========================================' -ForegroundColor Cyan
Write-Host ''

if (-not (Test-Path -LiteralPath $PropsPath)) {
    Write-Host "  EngineRoot.props not found at:" -ForegroundColor Red
    Write-Host "    $PropsPath"                    -ForegroundColor Red
    Write-Host ''
    exit 1
}

# ── Read current EngineRootDir from the .props ────────────────────────────────
[xml]$xml = Get-Content -LiteralPath $PropsPath -Raw -Encoding UTF8
$node = $xml.SelectSingleNode("//*[local-name()='EngineRootDir']")
if (-not $node) {
    Write-Host "  EngineRoot.props is malformed (no <EngineRootDir>)." -ForegroundColor Red
    exit 1
}
$current = $node.'#text'

# The manifest is project-owned metadata, so refresh it even when the engine is
# already frozen and the engine-copy phase is a no-op.
$manifestPath = Join-Path $ProjectDir 'ScriptManifest.txt'
[System.IO.File]::WriteAllLines($manifestPath, $scriptManifest, [System.Text.UTF8Encoding]::new($false))
Write-Host "  Lua scripts:   $($scriptManifest.Count)  (ScriptManifest.txt)" -ForegroundColor DarkGray
foreach ($script in $scriptManifest) { Write-Host "    $script" -ForegroundColor DarkGray }
Write-Host ''

# Already packaged? (literal "$(SolutionDir)")
if ($current -match '^\s*\$\(SolutionDir\)\s*$') {
    Write-Host '  Already packaged. Engine is local; nothing to do.' -ForegroundColor Green
    Write-Host ''
    exit 0
}

$EngineRoot = $current.TrimEnd('\','/')
if (-not (Test-Path -LiteralPath $EngineRoot)) {
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

$folders = @('Engine', 'ThirdParty', 'include', 'lib')
foreach ($folder in $folders) {
    $src = Join-Path $EngineRoot $folder
    if (-not (Test-Path -LiteralPath $src)) {
        Write-Host "    skip $folder\  (not found in engine repo)" -ForegroundColor DarkGray
        continue
    }
    Write-Host "    copy $folder\" -ForegroundColor DarkGray
    Copy-Item -LiteralPath $src -Destination (Join-Path $ProjectDir $folder) -Recurse -Force
}

# Runtime DLLs - refresh in case the engine added new ones since generation.
$binSrc = Join-Path $EngineRoot 'bin'
if (Test-Path -LiteralPath $binSrc) {
    $binDst = Join-Path $ProjectDir 'bin'
    if (-not (Test-Path -LiteralPath $binDst)) { New-Item -ItemType Directory -Path $binDst | Out-Null }
    Write-Host '    copy bin\*.dll' -ForegroundColor DarkGray
    foreach ($dll in Get-ChildItem -LiteralPath $binSrc -Filter '*.dll' -File) {
        Copy-Item -LiteralPath $dll.FullName -Destination $binDst -Force
    }
}

# OpenglViewer.props - the shared compile-flag sheet.
$propsSrc = Join-Path $EngineRoot 'OpenglViewer.props'
if (Test-Path -LiteralPath $propsSrc) {
    Write-Host '    copy OpenglViewer.props' -ForegroundColor DarkGray
    Copy-Item -LiteralPath $propsSrc -Destination $ProjectDir -Force
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
Write-Host '  Build Editor|Win32 or Game|Win32; bin\<Project>-Editor.exe and bin\<Project>-Game.exe stay separate.'
Write-Host ''
