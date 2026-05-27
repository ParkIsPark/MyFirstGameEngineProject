<#
.SYNOPSIS
    Create a standalone OpenGL project at a user-chosen location with a full
    copy of the engine. Unreal-style "GenerateProjectFiles" flow:

       1. GUI folder picker  -> pick parent directory
       2. GUI input box      -> enter project name
       3. Copy Engine/, include/, lib/, bin/, OpenglViewer.props to <picked>/<name>/
       4. Generate <name>.sln + <name>.vcxproj + <name>.vcxproj.filters + main.cpp

    The generated project is fully self-contained — moving the engine repo
    will not break the generated project.

.NOTES
    Designed to be launched by GenerateProject.bat from the repo root.
    Requires Windows PowerShell (uses System.Windows.Forms).
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName Microsoft.VisualBasic

# ── Locate engine root (parent of Scripts\) ───────────────────────────────────
$EngineRoot  = Split-Path -Parent $PSScriptRoot
$TemplateDir = Join-Path $EngineRoot 'Template'

if (-not (Test-Path (Join-Path $TemplateDir 'Template.vcxproj'))) {
    [System.Windows.Forms.MessageBox]::Show(
        "Cannot find Template\Template.vcxproj at:`n$TemplateDir",
        'Project Generator', 'OK', 'Error') | Out-Null
    return
}

Write-Host ''
Write-Host '  MyFirstGameEngine — Project Generator' -ForegroundColor Cyan
Write-Host '  =====================================' -ForegroundColor Cyan
Write-Host ''

# ── [1/4] Pick parent folder ──────────────────────────────────────────────────
Write-Host '  [1/4] Choose a parent folder for the new project...' -ForegroundColor Yellow

$dialog = New-Object System.Windows.Forms.FolderBrowserDialog
$dialog.Description = 'Select a parent folder. A new sub-folder will be created inside it for your project.'
$dialog.ShowNewFolderButton = $true
$dialog.SelectedPath = [Environment]::GetFolderPath('Desktop')

# FolderBrowserDialog must be shown from an STA thread; pwsh.exe is STA by default.
if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
    Write-Host '  Cancelled.' -ForegroundColor DarkGray
    return
}
$parent = $dialog.SelectedPath
Write-Host "        $parent" -ForegroundColor DarkGray

# ── [2/4] Ask for project name ────────────────────────────────────────────────
Write-Host '  [2/4] Enter a project name...' -ForegroundColor Yellow

$name = [Microsoft.VisualBasic.Interaction]::InputBox(
    "Enter the project name.`r`n`r`nIt will be used as the sub-folder name and the .sln / .vcxproj name.`r`nAllowed: letters, digits, underscore. Must not start with a digit.",
    'Project name',
    'MyProject')

if ([string]::IsNullOrWhiteSpace($name)) {
    Write-Host '  Cancelled.' -ForegroundColor DarkGray
    return
}

$name = $name.Trim()
if ($name -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') {
    [System.Windows.Forms.MessageBox]::Show(
        "Invalid project name: '$name'`n`nUse letters, digits, underscore. Must not start with a digit.",
        'Project Generator', 'OK', 'Error') | Out-Null
    return
}

$dest = Join-Path $parent $name
if (Test-Path $dest) {
    [System.Windows.Forms.MessageBox]::Show(
        "Destination already exists:`n$dest`n`nPick a different name or delete the existing folder.",
        'Project Generator', 'OK', 'Error') | Out-Null
    return
}

Write-Host "        $name  ->  $dest" -ForegroundColor DarkGray

# ── [3/4] Copy engine + props ─────────────────────────────────────────────────
Write-Host '  [3/4] Copying engine code...' -ForegroundColor Yellow
New-Item -ItemType Directory -Path $dest | Out-Null

$folders = @('Engine', 'include', 'lib', 'bin')
foreach ($folder in $folders) {
    $src = Join-Path $EngineRoot $folder
    if (-not (Test-Path $src)) { continue }
    Write-Host "        copy $folder\" -ForegroundColor DarkGray
    Copy-Item -Path $src -Destination (Join-Path $dest $folder) -Recurse -Force
}

$propsSrc = Join-Path $EngineRoot 'OpenglViewer.props'
Copy-Item -Path $propsSrc -Destination $dest -Force

# ── [4/4] Generate project files ──────────────────────────────────────────────
Write-Host '  [4/4] Generating project files...' -ForegroundColor Yellow

$projGuid     = [System.Guid]::NewGuid().ToString().ToUpper()
$slnGuid      = [System.Guid]::NewGuid().ToString().ToUpper()
$projTypeGuid = '8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942'

# Patch GUID + RootNamespace into a copy of Template.vcxproj.
$vcxSrc = Join-Path $TemplateDir 'Template.vcxproj'
$vcxDst = Join-Path $dest "$name.vcxproj"
(Get-Content $vcxSrc -Raw -Encoding UTF8) `
    -replace 'AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE', $projGuid `
    -replace '<RootNamespace>Template</RootNamespace>', "<RootNamespace>$name</RootNamespace>" |
    Set-Content $vcxDst -Encoding UTF8

# Filters file has no GUID / name to patch — copy as-is.
Copy-Item (Join-Path $TemplateDir 'Template.vcxproj.filters') (Join-Path $dest "$name.vcxproj.filters")

# main.cpp stub.
Copy-Item (Join-Path $TemplateDir 'main.cpp') (Join-Path $dest 'main.cpp')

# Build the .sln content. Tabs are required by the .sln format.
$slnLines = @(
    'Microsoft Visual Studio Solution File, Format Version 12.00'
    '# Visual Studio Version 17'
    'VisualStudioVersion = 17.0.31903.59'
    'MinimumVisualStudioVersion = 10.0.40219.1'
    "Project(`"{$projTypeGuid}`") = `"$name`", `"$name.vcxproj`", `"{$projGuid}`""
    'EndProject'
    'Global'
    "`tGlobalSection(SolutionConfigurationPlatforms) = preSolution"
    "`t`tDebug|Win32 = Debug|Win32"
    "`t`tRelease|Win32 = Release|Win32"
    "`tEndGlobalSection"
    "`tGlobalSection(ProjectConfigurationPlatforms) = postSolution"
    "`t`t{$projGuid}.Debug|Win32.ActiveCfg = Debug|Win32"
    "`t`t{$projGuid}.Debug|Win32.Build.0 = Debug|Win32"
    "`t`t{$projGuid}.Release|Win32.ActiveCfg = Release|Win32"
    "`t`t{$projGuid}.Release|Win32.Build.0 = Release|Win32"
    "`tEndGlobalSection"
    "`tGlobalSection(SolutionProperties) = preSolution"
    "`t`tHideSolutionNode = FALSE"
    "`tEndGlobalSection"
    "`tGlobalSection(ExtensibilityGlobals) = postSolution"
    "`t`tSolutionGuid = {$slnGuid}"
    "`tEndGlobalSection"
    'EndGlobal'
)

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllLines((Join-Path $dest "$name.sln"), $slnLines, $utf8NoBom)

# ── Done ──────────────────────────────────────────────────────────────────────
Write-Host ''
Write-Host '  Done!' -ForegroundColor Green
Write-Host "  $dest" -ForegroundColor Green
Write-Host "    $name.sln"
Write-Host "    $name.vcxproj"
Write-Host "    $name.vcxproj.filters"
Write-Host '    OpenglViewer.props'
Write-Host '    main.cpp'
Write-Host '    Engine\, include\, lib\, bin\   (copied from the engine repo)'
Write-Host ''

# Offer to open the .sln in Visual Studio.
$openIt = [System.Windows.Forms.MessageBox]::Show(
    "Project created:`n$dest`n`nOpen $name.sln in Visual Studio now?",
    'Project Generator',
    'YesNo', 'Information')

if ($openIt -eq [System.Windows.Forms.DialogResult]::Yes) {
    Start-Process (Join-Path $dest "$name.sln")
}
