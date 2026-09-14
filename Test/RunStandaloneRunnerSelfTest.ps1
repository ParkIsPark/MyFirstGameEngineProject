<# Run: powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneRunnerSelfTest.ps1
Proves runner failures keep logs/status and remove owned artifacts/processes.
Compiler cases exercise actual gcc, g++ and cmd/MSVC failures on Windows PowerShell 5.1.
#>
[CmdletBinding()]
param([string]$LogDirectory = '', [switch]$CompileOnly)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root
if (-not $LogDirectory) { $LogDirectory = Join-Path ([IO.Path]::GetTempPath()) ('runner-evidence-' + [Guid]::NewGuid()) }
New-Item -ItemType Directory -Force -Path $LogDirectory | Out-Null
$logRoot = (Resolve-Path -LiteralPath $LogDirectory).Path
$compilerCases = @('Compile','CppCompile','MSVCCompile')
$cases = if ($CompileOnly) { $compilerCases } else { $compilerCases + @('ProcessStart','NonzeroExit','Timeout','AfterStart') }
$failures = 0
foreach ($case in $cases) {
    $caseLogs = Join-Path $logRoot $case
    New-Item -ItemType Directory -Path $caseLogs -Force | Out-Null
    $oldPrefix = $env:GCC_EXEC_PREFIX
    $oldCppInclude = $env:CPLUS_INCLUDE_PATH
    $oldClOptions = $env:_CL_
    $compilerFixture = $null
    try {
        $args = @('-NoProfile','-ExecutionPolicy','Bypass','-File',"$PSScriptRoot/RunStandaloneTests.ps1",'-LogDirectory',$caseLogs)
        if ($case -eq 'Compile') {
            $env:GCC_EXEC_PREFIX = (Join-Path $caseLogs 'nonexistent-compiler-prefix/')
            $args += @('-Name','LuaScriptInstanceTest')
        } elseif ($case -in @('CppCompile','MSVCCompile')) {
            $compilerFixture = Join-Path ([IO.Path]::GetTempPath()) ('rt-compiler-' + [Guid]::NewGuid())
            New-Item -ItemType Directory -Path $compilerFixture | Out-Null
            $header = Join-Path $compilerFixture 'iostream'
            [IO.File]::WriteAllText($header, '#error task13_cpp_failure_probe')
            if ($case -eq 'CppCompile') {
                $env:CPLUS_INCLUDE_PATH = $compilerFixture
                $args += @('-Name','LuaScriptInstanceTest')
            } else {
                $env:_CL_ = '/FI"' + $header + '"'
                $args += @('-Name','FeatureLifecycleTest')
            }
        } else { $args += @('-FailureProbe',$case) }
        $ErrorActionPreference = 'Continue' # Native stderr is evidence, not a harness exception.
        & "$env:WINDIR/System32/WindowsPowerShell/v1.0/powershell.exe" @args *> (Join-Path $caseLogs 'runner.log')
        $code = $LASTEXITCODE
    } finally {
        $env:GCC_EXEC_PREFIX = $oldPrefix
        $env:CPLUS_INCLUDE_PATH = $oldCppInclude
        $env:_CL_ = $oldClOptions
        $ErrorActionPreference = 'Stop'
        if ($compilerFixture) {
            $resolved = (Resolve-Path -LiteralPath $compilerFixture).Path
            if (-not $resolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()), [StringComparison]::OrdinalIgnoreCase) -or
                (Split-Path -Leaf $resolved) -notlike 'rt-compiler-*') { throw 'Unsafe compiler fixture path' }
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
    $manifest = Get-Content -LiteralPath (Join-Path $caseLogs 'runner-artifact.json') -Raw | ConvertFrom-Json
    $remaining = @(Get-Item -LiteralPath $manifest.ArtifactDirectory -ErrorAction SilentlyContinue)
    $okay = $code -ne 0 -and $remaining.Count -eq 0
    if ($case -in $compilerCases) {
        $stem = if ($case -eq 'MSVCCompile') { 'FeatureLifecycleTest' } else { 'LuaScriptInstanceTest' }
        $buildLog = Join-Path $caseLogs ($stem + '.build.log')
        $okay = $okay -and (Test-Path -LiteralPath $buildLog) -and (Get-Item -LiteralPath $buildLog).Length -gt 0
        $statusPath = Join-Path $caseLogs ($stem + '.build.status.json')
        $buildStatus = if (Test-Path -LiteralPath $statusPath) { Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json } else { $null }
        $expected = if ($case -eq 'MSVCCompile') { 2 } else { 1 }
        $okay = $okay -and $buildStatus -and $buildStatus.ExitCode -eq $expected
        if ($case -ne 'Compile') { $okay = $okay -and (Get-Content -LiteralPath $buildLog -Raw) -match 'task13_cpp_failure_probe' }
    } else {
        $summary = Get-Content -LiteralPath (Join-Path $caseLogs 'FailureProbe.status.json') -Raw | ConvertFrom-Json
        $expected = if ($case -eq 'NonzeroExit') { 7 } elseif ($case -eq 'Timeout') { 124 } else { 1 }
        $okay = $okay -and $summary.ExitCode -eq $expected -and $summary.Outcome -eq $case
        if ($summary.ProcessId) { $okay = $okay -and -not (Get-Process -Id $summary.ProcessId -ErrorAction SilentlyContinue) }
        $out = Join-Path $caseLogs 'FailureProbe.run.log'
        $err = Join-Path $caseLogs 'FailureProbe.run.err'
        $okay = $okay -and (Test-Path -LiteralPath $out) -and (Test-Path -LiteralPath $err)
        if ($case -in @('NonzeroExit','Timeout')) {
            $okay = $okay -and (Get-Content $out -Raw).Contains('probe stdout') -and (Get-Content $err -Raw).Contains('probe stderr')
        }
    }
    if (-not $okay) { ++$failures; Write-Output "FAIL $case exit=$code leakedArtifactDirectories=$($remaining.Count)" }
    else { Write-Output "PASS $case preserves failing status/logs and cleans artifacts/process" }
    # Delete only the exact artifact directory reported by this child, never a
    # glob/difference that could include a concurrently running user's test.
    foreach ($directory in $remaining) {
        $resolved = (Resolve-Path -LiteralPath $directory.FullName).Path
        $temp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
        if (-not $resolved.StartsWith($temp, [StringComparison]::OrdinalIgnoreCase) -or $directory.Name -notlike 'rt-tests-*') { throw 'Unsafe leaked fixture cleanup' }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
Write-Output "Runner failure selftests: $($cases.Count) cases, $failures failures. Evidence: $logRoot"
if ($failures) { exit 1 }
