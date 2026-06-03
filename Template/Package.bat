@echo off
REM -----------------------------------------------------------------------
REM  Package.bat - freeze the linked engine into this project.
REM
REM  After running this:
REM    - Engine\, include\, lib\, OpenglViewer.props are copied into the
REM      project folder.
REM    - EngineRoot.props is rewritten so $(EngineRootDir) = $(SolutionDir).
REM    - The project is fully self-contained; moving / deleting the engine
REM      repo will not break it. Suitable for homework submission.
REM
REM  Re-running on an already-packaged project is a safe no-op.
REM -----------------------------------------------------------------------
setlocal
pushd "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Package.ps1"
set _ec=%ERRORLEVEL%
popd
if not "%1"=="/silent" pause
endlocal & exit /b %_ec%
