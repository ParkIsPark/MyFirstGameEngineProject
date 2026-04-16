<#
.SYNOPSIS
    OpenglViewer.sln 에 새 프로젝트를 추가하고 Source/<Name>/ 를 생성합니다.

.DESCRIPTION
    - Source/<Name>/<Name>.vcxproj + main.cpp + <Name>.vcxproj.filters 생성
    - OpenglViewer.sln 에 프로젝트를 등록 (독립 .sln 은 만들지 않음)
    - $(SolutionDir) == 리포 루트이므로 모든 경로가 즉시 동작합니다.

.EXAMPLE
    .\new-project.ps1 HW4
    .\new-project.ps1 -Name RayTracing2
#>

param(
    [Parameter(Mandatory=$true, Position=0)]
    [string]$Name
)

$RepoRoot    = $PSScriptRoot
$DestDir     = Join-Path $RepoRoot "Source\$Name"
$TemplateDir = Join-Path $RepoRoot "Template"
$SlnPath     = Join-Path $RepoRoot "OpenglViewer.sln"

# ── Guard ──────────────────────────────────────────────────────────────────
if (Test-Path $DestDir) {
    Write-Error "Source\$Name 이미 존재합니다. 다른 이름을 사용하세요."
    exit 1
}

# ── 새 GUID ────────────────────────────────────────────────────────────────
$Guid = [System.Guid]::NewGuid().ToString().ToUpper()

New-Item -ItemType Directory -Path $DestDir | Out-Null

# ── .vcxproj 복사 + 패치 ───────────────────────────────────────────────────
$VcxprojSrc = Join-Path $TemplateDir "Template.vcxproj"
$VcxprojDst = Join-Path $DestDir     "$Name.vcxproj"

(Get-Content $VcxprojSrc -Encoding UTF8) `
    -replace 'AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE', $Guid `
    -replace '<RootNamespace>Template</RootNamespace>', "<RootNamespace>$Name</RootNamespace>" |
    Set-Content $VcxprojDst -Encoding UTF8

# ── .vcxproj.filters 복사 ──────────────────────────────────────────────────
$FiltersSrc = Join-Path $TemplateDir "Template.vcxproj.filters"
$FiltersDst = Join-Path $DestDir     "$Name.vcxproj.filters"
Copy-Item $FiltersSrc $FiltersDst

# ── main.cpp stub 복사 ────────────────────────────────────────────────────
Copy-Item (Join-Path $TemplateDir "main.cpp") (Join-Path $DestDir "main.cpp")

# ── OpenglViewer.sln 에 프로젝트 추가 ────────────────────────────────────
#
#  .sln 포맷:
#    Project("...") = "Name", "relative\path.vcxproj", "{GUID}"
#    EndProject
#    Global
#      GlobalSection(ProjectConfigurationPlatforms) = postSolution
#        {GUID}.Debug|Win32.ActiveCfg = Debug|Win32
#        ...
#      EndGlobalSection

$RelVcxproj  = "Source\$Name\$Name.vcxproj"
$ProjTypeGuid = "{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}"

# 줄 단위로 읽어서 삽입 위치를 정확히 제어
$lines   = [System.IO.File]::ReadAllLines($SlnPath)
$result  = [System.Collections.Generic.List[string]]::new()

$projInserted   = $false
$configInserted = $false
$inProjConfig   = $false

foreach ($line in $lines) {

    # "Global" 행 직전에 Project 블록 삽입
    if (-not $projInserted -and $line -match '^Global\s*$') {
        $result.Add("Project(`"$ProjTypeGuid`") = `"$Name`", `"$RelVcxproj`", `"{$Guid}`"")
        $result.Add("EndProject")
        $projInserted = $true
    }

    $result.Add($line)

    # ProjectConfigurationPlatforms 섹션 시작 감지
    if ($line -match 'GlobalSection\(ProjectConfigurationPlatforms\)') {
        $inProjConfig = $true
    }

    # 섹션 종료 직전에 config 항목 삽입
    if ($inProjConfig -and -not $configInserted -and $line -match '^\s*EndGlobalSection') {
        # 방금 추가한 EndGlobalSection 앞에 4줄 삽입
        $insertAt = $result.Count - 1
        $result.Insert($insertAt, "`t`t{$Guid}.Debug|Win32.ActiveCfg = Debug|Win32")
        $result.Insert($insertAt + 1, "`t`t{$Guid}.Debug|Win32.Build.0 = Debug|Win32")
        $result.Insert($insertAt + 2, "`t`t{$Guid}.Release|Win32.ActiveCfg = Release|Win32")
        $result.Insert($insertAt + 3, "`t`t{$Guid}.Release|Win32.Build.0 = Release|Win32")
        $configInserted = $true
        $inProjConfig   = $false
    }
}

# BOM 없는 UTF-8 로 덮어쓰기 (.sln 표준 인코딩)
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllLines($SlnPath, $result, $utf8NoBom)

# ── 결과 출력 ─────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "  Source\$Name\" -ForegroundColor Green
Write-Host "    $Name.vcxproj"
Write-Host "    $Name.vcxproj.filters"
Write-Host "    main.cpp"
Write-Host ""
Write-Host "  OpenglViewer.sln 에 '$Name' 추가됨 (GUID: $Guid)" -ForegroundColor Green
Write-Host ""
Write-Host "  OpenglViewer.sln 을 Visual Studio 에서 다시 로드하면 바로 보입니다." -ForegroundColor Cyan
