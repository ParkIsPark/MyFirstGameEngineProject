@echo off
REM ====================================================================
REM  MyFirstGameEngine - Project Generator (Unreal-style)
REM
REM  Double-click this file to:
REM    1. Pick a parent folder via a GUI dialog
REM    2. Type a project name
REM    3. Get a fully self-contained project (engine code copied in)
REM ====================================================================

title MyFirstGameEngine - Project Generator

REM Use STA so System.Windows.Forms dialogs work correctly.
powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0Scripts\GenerateProject.ps1"

echo.
echo ----------------------------------------------------------------
pause
