<#
.SYNOPSIS
    Scaffold a new homework project under Source/<Name>/.

.EXAMPLE
    .\new-project.ps1 HW4
    .\new-project.ps1 -Name RayTracing2
#>

param(
    [Parameter(Mandatory=$true, Position=0)]
    [string]$Name
)

$RepoRoot   = $PSScriptRoot
$DestDir    = Join-Path $RepoRoot "Source\$Name"
$TemplateDir= Join-Path $RepoRoot "Template"

# --- Guard against overwriting existing work ---
if (Test-Path $DestDir) {
    Write-Error "Source\$Name already exists. Choose a different name."
    exit 1
}

# --- Generate a fresh GUID for the new project ---
$Guid = [System.Guid]::NewGuid().ToString().ToUpper()

New-Item -ItemType Directory -Path $DestDir | Out-Null

# --- Copy and patch the .vcxproj ---
$VcxprojSrc = Join-Path $TemplateDir "Template.vcxproj"
$VcxprojDst = Join-Path $DestDir     "$Name.vcxproj"

(Get-Content $VcxprojSrc) `
    -replace 'AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE', $Guid `
    -replace '<RootNamespace>Template</RootNamespace>', "<RootNamespace>$Name</RootNamespace>" |
    Set-Content $VcxprojDst

# --- Copy main.cpp stub ---
Copy-Item (Join-Path $TemplateDir "main.cpp") (Join-Path $DestDir "main.cpp")

# --- Generate a minimal standalone .sln ---
$SlnContent = @"

Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 17
VisualStudioVersion = 17.13.35913.81 d17.13
MinimumVisualStudioVersion = 10.0.40219.1
Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "$Name", "$Name.vcxproj", "{$Guid}"
EndProject
Global
	GlobalSection(SolutionConfigurationPlatforms) = preSolution
		Debug|Win32 = Debug|Win32
		Release|Win32 = Release|Win32
	EndGlobalSection
	GlobalSection(ProjectConfigurationPlatforms) = postSolution
		{$Guid}.Debug|Win32.ActiveCfg = Debug|Win32
		{$Guid}.Debug|Win32.Build.0 = Debug|Win32
		{$Guid}.Release|Win32.ActiveCfg = Release|Win32
		{$Guid}.Release|Win32.Build.0 = Release|Win32
	EndGlobalSection
	GlobalSection(SolutionProperties) = preSolution
		HideSolutionNode = FALSE
	EndGlobalSection
EndGlobal
"@

$SlnPath = Join-Path $DestDir "$Name.sln"
Set-Content $SlnPath $SlnContent

Write-Host ""
Write-Host "Created Source\$Name\" -ForegroundColor Green
Write-Host "  $Name.vcxproj  (project GUID: $Guid)"
Write-Host "  $Name.sln      (standalone solution)"
Write-Host "  main.cpp"
Write-Host ""
Write-Host "Open Source\$Name\$Name.sln in Visual Studio and start coding." -ForegroundColor Cyan
Write-Host "All Engine headers and .cpp files are pre-wired via OpenglViewer.props." -ForegroundColor Cyan
