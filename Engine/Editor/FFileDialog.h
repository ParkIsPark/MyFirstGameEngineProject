#pragma once
#include <string>

// ---------------------------------------------------------------------------
// FFileDialog — native Win32 "Open File" dialog (GetOpenFileName), isolated in
// its own translation unit so <windows.h>/<commdlg.h> never leak their macros
// (min/max/APIENTRY) into the GL/ImGui editor code. comdlg32 is linked via a
// #pragma comment, so no .vcxproj/.props change is needed.
// ---------------------------------------------------------------------------
namespace FFileDialog
{
    // Opens the OS file picker filtered to importable assets (.obj/.fbx/.world).
    // Returns the chosen absolute path, or "" if the user cancelled.
    std::string OpenAsset();
}
