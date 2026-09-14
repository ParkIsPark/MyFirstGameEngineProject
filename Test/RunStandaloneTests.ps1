<#
Current reproducible standalone test recipe (run at repository root).
First build Engine.sln Debug|Win32 with /m:1 /nr:false and _CL_=/FS.
Run all: powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1
Run one: powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name RenderPerformanceInvariantTest
VS2022 Community + MSYS2 UCRT64 are the compiler locations used here.
LuaScriptInstanceTest additionally uses GNU import wrapping to test the real
Windows file-replacement race; all other sources link the real Debug Engine.lib.
Each standalone EXE has a 60-second watchdog. Logs use a unique temp directory
or -LogDirectory; artifacts always use a short temporary directory and are
removed on completion. No standalone main is added to Test.vcxproj.
#>
[CmdletBinding()]
param([string]$Name = '', [string]$LogDirectory = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root
if ($Name -and $Name -notmatch '^[A-Za-z0-9_]+$') { throw 'Name must be a test stem' }
if (-not (Test-Path -LiteralPath 'bin/Engine.lib')) { throw 'Build Engine.sln Debug|Win32 first' }
if (-not $LogDirectory) { $LogDirectory = Join-Path ([IO.Path]::GetTempPath()) ('renderer-standalone-' + [Guid]::NewGuid()) }
$logRoot = [IO.Path]::GetFullPath($LogDirectory)
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$artifactRoot = Join-Path $temporaryRoot ('rt-tests-' + [Guid]::NewGuid())
New-Item -ItemType Directory -Path $artifactRoot | Out-Null
$includes = @('include', 'ThirdParty/Lua/5.4.9/src') + @((Get-ChildItem Engine -Directory -Recurse).FullName)
$includeFlags = ($includes | ForEach-Object { '/I"' + $_ + '"' }) -join ' '
$tests = @(Get-ChildItem -LiteralPath Test -Filter '*.cpp' | Where-Object {
    $_.BaseName -ne 'main' -and (Get-Content -Raw -LiteralPath $_.FullName) -match '\bmain\s*\(' -and
    (-not $Name -or $_.BaseName -eq $Name)
} | Sort-Object Name)
if ($tests.Count -eq 0) { throw 'No matching standalone tests' }
$env:Path = "$root\bin;C:\msys64\ucrt64\bin;$env:Path"
$failures = 0
foreach ($test in $tests) {
    $stem = $test.BaseName
    $exe = Join-Path $artifactRoot ($stem + '.exe')
    $obj = Join-Path $artifactRoot ($stem + '.obj')
    $buildLog = Join-Path $logRoot ($stem + '.build.log')
    if ($stem -eq 'LuaScriptInstanceTest') {
        # Preserve GNU __imp_GetFinalPathNameByHandleW wrapping coverage.
        $luaFiles = Get-ChildItem ThirdParty/Lua/5.4.9/src -Filter '*.c' | Where-Object { $_.Name -notin @('lua.c', 'luac.c') }
        $luaObjects = @()
        foreach ($lua in $luaFiles) {
            $luaObj = Join-Path $artifactRoot ($lua.BaseName + '.o')
            & C:/msys64/ucrt64/bin/gcc.exe -std=c17 -DLUA_USE_APICHECK -I./ThirdParty/Lua/5.4.9/src -c $lua.FullName -o $luaObj *>> $buildLog
            if ($LASTEXITCODE -ne 0) { throw "Lua compile failed: $($lua.Name)" }
            $luaObjects += $luaObj
        }
        & C:/msys64/ucrt64/bin/g++.exe -std=c++17 -Wall -Wextra -I./Engine/Script -I./ThirdParty/Lua/5.4.9/src $test.FullName Engine/Script/FScriptPath.cpp Engine/Script/FLuaBindingRegistry.cpp Engine/Script/UScriptSubsystem.cpp Engine/Script/FLuaScriptCache.cpp Engine/Script/FLuaScriptInstance.cpp @luaObjects '-Wl,--wrap=__imp_GetFinalPathNameByHandleW' -o $exe *>> $buildLog
    } else {
        $command = 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 >nul && cl /nologo /utf-8 /FS /std:c++17 /EHsc /MDd /DWIN32 /D_DEBUG ' + $includeFlags + ' "' + $test.FullName + '" /Fo:"' + $obj + '" /Fe:"' + $exe + '" bin\Engine.lib /link /OPT:NOREF,NOICF /LIBPATH:lib glew32.lib freeglut.lib glfw3dll.lib opengl32.lib glu32.lib assimp-vc143-mt.lib'
        & cmd.exe /d /c $command *> $buildLog
    }
    $compileExit = $LASTEXITCODE
    if ($compileExit -ne 0) {
        Write-Host "FAIL $stem compile=$compileExit"
        Get-Content $buildLog -Tail 12
        ++$failures
        continue
    }
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $exe
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(60000)) {
        Stop-Process -Id $process.Id
        Write-Host "FAIL $stem timeout=60s"; ++$failures; continue
    }
    $exitCode = $process.ExitCode
    [IO.File]::WriteAllText((Join-Path $logRoot ($stem + '.run.log')), $stdout.Result)
    [IO.File]::WriteAllText((Join-Path $logRoot ($stem + '.run.err')), $stderr.Result)
    $process.Dispose()
    if ($exitCode -ne 0) { ++$failures }
    Write-Host "$stem compile=0 run=$exitCode"
    Get-Content (Join-Path $logRoot ($stem + '.run.log')) -Tail 2
}
Write-Host "Standalone matrix: $($tests.Count) sources, $failures failures. Evidence: $logRoot"
$resolvedArtifacts = (Resolve-Path -LiteralPath $artifactRoot).Path
if (-not $resolvedArtifacts.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path -Leaf $resolvedArtifacts) -notlike 'rt-tests-*') { throw 'Unexpected artifact cleanup path' }
Remove-Item -LiteralPath $resolvedArtifacts -Recurse -Force
if ($failures) { exit 1 }
