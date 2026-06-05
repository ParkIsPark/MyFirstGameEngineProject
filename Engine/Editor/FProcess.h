#pragma once
#include <string>

// ---------------------------------------------------------------------------
// FProcess — minimal Win32 process helpers, isolated in their own TU so
// <windows.h> macros never leak into the GL/ImGui editor code (same pattern as
// FFileDialog).
// ---------------------------------------------------------------------------
namespace FProcess
{
    // Full path of the currently running executable (GetModuleFileName).
    std::string ExecutablePath();

    // Launch `exe args` as a new, detached process (own window). Returns true if
    // the process was created. The child inherits this process's working dir, so
    // relative paths in `args` resolve the same way.
    bool LaunchDetached(const std::string& exe, const std::string& args);
}
