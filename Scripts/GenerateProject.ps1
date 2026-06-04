<#
.SYNOPSIS
    Create a new project that REFERENCES this engine by path (linked mode) so
    engine edits propagate immediately. Run Package.bat inside the project to
    freeze the engine in for submission.

    Generated layout (Unreal-ish):
        <name>/
          <name>.sln  <name>.vcxproj  <name>.vcxproj.filters
          <name>.proj            (manifest: ProjectName/EngineVersion)
          EngineRoot.props       (absolute path to this engine repo)
          Package.bat Package.ps1
          Source/main.cpp        (Editor/Game dispatch)
          Setting/DefaultEngine.ini DefaultGame.ini DefaultInput.ini
          Content/DefaultWorld.world   (starter world)
          bin/  *.dll                  (runtime DLLs)

    Configurations: Editor|Win32 (ImGui editor) and Game|Win32 (GAME_BUILD ->
    standalone game). Both build <name>.exe into bin\.

.PARAMETER Parent
    Parent folder for the new project. If omitted, a GUI folder picker is shown.
.PARAMETER Name
    Project name. If omitted, a GUI input box is shown. Passing both Parent and
    Name runs fully headless (no dialogs) -- used for scripted/CI generation.
#>

[CmdletBinding()]
param(
    [string]$Parent = '',
    [string]$Name   = ''
)

$ErrorActionPreference = 'Stop'

$EngineRoot  = Split-Path -Parent $PSScriptRoot
$TemplateDir = Join-Path $EngineRoot 'Template'
$Headless    = ($Parent -ne '' -and $Name -ne '')

function Fail($msg) {
    if ($Headless) { Write-Error $msg }
    else {
        Add-Type -AssemblyName System.Windows.Forms
        [System.Windows.Forms.MessageBox]::Show($msg, 'Project Generator', 'OK', 'Error') | Out-Null
    }
    exit 1
}

if (-not (Test-Path (Join-Path $TemplateDir 'Template.vcxproj'))) {
    Fail "Cannot find Template\Template.vcxproj at: $TemplateDir"
}

# ── [1/4] parent folder ───────────────────────────────────────────────────────
if (-not $Headless) {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName Microsoft.VisualBasic
    Write-Host '  [1/4] Choose a parent folder...' -ForegroundColor Yellow
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Select a parent folder; a sub-folder is created for your project.'
    $dialog.ShowNewFolderButton = $true
    $dialog.SelectedPath = [Environment]::GetFolderPath('Desktop')
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { Write-Host '  Cancelled.'; return }
    $Parent = $dialog.SelectedPath
}

# ── [2/4] project name ────────────────────────────────────────────────────────
if (-not $Headless) {
    Write-Host '  [2/4] Enter a project name...' -ForegroundColor Yellow
    $Name = [Microsoft.VisualBasic.Interaction]::InputBox(
        "Project name (letters/digits/underscore, not starting with a digit).",
        'Project name', 'MyProject')
    if ([string]::IsNullOrWhiteSpace($Name)) { Write-Host '  Cancelled.'; return }
}

$Name = $Name.Trim()
if ($Name -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') { Fail "Invalid project name: '$Name'" }
if (-not (Test-Path $Parent)) { Fail "Parent folder does not exist: $Parent" }

$dest = Join-Path $Parent $Name
if (Test-Path $dest) { Fail "Destination already exists: $dest" }

Write-Host "  Generating '$Name' -> $dest" -ForegroundColor Cyan

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
function WriteText($path, $text) { [System.IO.File]::WriteAllText($path, $text, $utf8NoBom) }

# ── [3/4] folders + files ─────────────────────────────────────────────────────
New-Item -ItemType Directory -Path $dest | Out-Null
foreach ($sub in @('Setting', 'Content', 'bin')) {
    New-Item -ItemType Directory -Path (Join-Path $dest $sub) | Out-Null
}

# runtime DLLs
$binSrc = Join-Path $EngineRoot 'bin'
if (Test-Path $binSrc) {
    Get-ChildItem -Path $binSrc -Filter '*.dll' -File | Copy-Item -Destination (Join-Path $dest 'bin') -Force
}

# EngineRoot.props (linked: absolute path back to this repo)
$engineRootForProps = $EngineRoot
if (-not $engineRootForProps.EndsWith('\')) { $engineRootForProps += '\' }
$propsXml = @"
<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup Label="UserMacros">
    <EngineRootDir>$engineRootForProps</EngineRootDir>
  </PropertyGroup>
  <ItemGroup>
    <BuildMacro Include="EngineRootDir">
      <Value>`$(EngineRootDir)</Value>
    </BuildMacro>
  </ItemGroup>
</Project>
"@
WriteText (Join-Path $dest 'EngineRoot.props') $propsXml

# .vcxproj (patch GUID + RootNamespace)
$projGuid     = [System.Guid]::NewGuid().ToString().ToUpper()
$slnGuid      = [System.Guid]::NewGuid().ToString().ToUpper()
$projTypeGuid = '8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942'

$vcx = (Get-Content (Join-Path $TemplateDir 'Template.vcxproj') -Raw -Encoding UTF8) `
    -replace 'AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE', $projGuid `
    -replace '<RootNamespace>Template</RootNamespace>', "<RootNamespace>$Name</RootNamespace>"
WriteText (Join-Path $dest "$Name.vcxproj") $vcx

Copy-Item (Join-Path $TemplateDir 'Template.vcxproj.filters') (Join-Path $dest "$Name.vcxproj.filters")

# main.cpp (substitute project name)
$main = (Get-Content (Join-Path $TemplateDir 'main.cpp') -Raw -Encoding UTF8) `
    -replace '__PROJECT_NAME__', $Name
WriteText (Join-Path $dest 'main.cpp') $main

# Setting\*.ini (copy templates)
$settingSrc = Join-Path $TemplateDir 'Setting'
if (Test-Path $settingSrc) {
    Get-ChildItem -Path $settingSrc -Filter '*.ini' -File | Copy-Item -Destination (Join-Path $dest 'Setting') -Force
}

# <name>.proj manifest
WriteText (Join-Path $dest "$Name.proj") "ProjectName = $Name`nEngineVersion = 1.0`n"

# Package.bat / .ps1
Copy-Item (Join-Path $TemplateDir 'Package.bat') (Join-Path $dest 'Package.bat')
Copy-Item (Join-Path $TemplateDir 'Package.ps1') (Join-Path $dest 'Package.ps1')

# Content\DefaultWorld.world (starter: a sphere + a point light)
$world = @'
WorldFormat = 1

[World]
ShadingModel = 2
RenderMode = 0
FloorEnabled = 0
FloorY = -2.5
Gravity = 0 -9.8 0

[Camera]
Position = 0 1 6
Yaw = 0
Pitch = -5
Fov = 60

[Actor]
Type = Actor
Name = Sphere
Loc = 0 1 0
Rot = 0 0 0
Scale = 1 1 1
  [Component]
Type = Mesh
Name =
Loc = 0 0 0
Rot = 0 0 0
Scale = 1 1 1
Mesh = Sphere 1.5 28 14
HasMatOverride = 1
kd = 0.5 0.6 0.9
ks = 0.4 0.4 0.4
ka = 0.2 0.2 0.2
shininess = 32
km = 0 0 0
emissive = 0 0 0
diffuseTex =
wrap = 0
uvTiling = 1 1

[Actor]
Type = Light
Name = PointLight
Loc = 4 5 3
Rot = 0 0 0
Scale = 1 1 1
  [Component]
Type = PointLight
Name =
Loc = 0 0 0
Rot = 0 0 0
Scale = 1 1 1
LightColor = 1 1 1
LightIntensity = 1 1 1
'@
WriteText (Join-Path $dest 'Content\DefaultWorld.world') $world

# ── [4/4] .sln (Editor|Win32 + Game|Win32) ────────────────────────────────────
$slnLines = @(
    'Microsoft Visual Studio Solution File, Format Version 12.00'
    '# Visual Studio Version 17'
    'VisualStudioVersion = 17.0.31903.59'
    'MinimumVisualStudioVersion = 10.0.40219.1'
    "Project(`"{$projTypeGuid}`") = `"$Name`", `"$Name.vcxproj`", `"{$projGuid}`""
    'EndProject'
    'Global'
    "`tGlobalSection(SolutionConfigurationPlatforms) = preSolution"
    "`t`tEditor|Win32 = Editor|Win32"
    "`t`tGame|Win32 = Game|Win32"
    "`tEndGlobalSection"
    "`tGlobalSection(ProjectConfigurationPlatforms) = postSolution"
    "`t`t{$projGuid}.Editor|Win32.ActiveCfg = Editor|Win32"
    "`t`t{$projGuid}.Editor|Win32.Build.0 = Editor|Win32"
    "`t`t{$projGuid}.Game|Win32.ActiveCfg = Game|Win32"
    "`t`t{$projGuid}.Game|Win32.Build.0 = Game|Win32"
    "`tEndGlobalSection"
    "`tGlobalSection(SolutionProperties) = preSolution"
    "`t`tHideSolutionNode = FALSE"
    "`tEndGlobalSection"
    "`tGlobalSection(ExtensibilityGlobals) = postSolution"
    "`t`tSolutionGuid = {$slnGuid}"
    "`tEndGlobalSection"
    'EndGlobal'
)
[System.IO.File]::WriteAllLines((Join-Path $dest "$Name.sln"), $slnLines, $utf8NoBom)

Write-Host "  Done: $dest" -ForegroundColor Green
Write-Host "    $Name.sln  ($Name.vcxproj: Editor|Win32, Game|Win32)"
Write-Host '    main.cpp  Setting\*.ini  Content\DefaultWorld.world  bin\*.dll'

if (-not $Headless) {
    Add-Type -AssemblyName System.Windows.Forms
    $open = [System.Windows.Forms.MessageBox]::Show(
        "Project created (linked mode):`n$dest`n`nOpen $Name.sln now?",
        'Project Generator', 'YesNo', 'Information')
    if ($open -eq [System.Windows.Forms.DialogResult]::Yes) { Start-Process (Join-Path $dest "$Name.sln") }
}
