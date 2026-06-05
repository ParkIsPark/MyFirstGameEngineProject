#pragma once
#include <string>

// Render mode requested by a project. CPU_RT is kept for plan compatibility
// even though the CPU ray tracer was removed -- it falls back to GPU_RT.
enum class EProjectRenderMode { CPU_RT, GPU_RT, Rasterizer, Hybrid };

// ---------------------------------------------------------------------------
// FProjectDescriptor (E3) — minimal ".proj" descriptor.
//
// A .proj is a flat `Key = Value` text file describing window + render setup:
//
//     WindowTitle  = My Project
//     Width        = 1280
//     Height       = 720
//     RenderMode   = GPU_RT        # CPU_RT | GPU_RT | Rasterizer | Hybrid
//     StartupWorld =               # empty -> WorldSetting() code hook
//
// Robustness is a hard requirement: a missing file, unreadable file, or
// garbage lines must NEVER crash -- unknown/absent keys keep their defaults.
// ---------------------------------------------------------------------------
struct FProjectDescriptor
{
    // .proj manifest (identity)
    std::string projectName   = "Project";
    std::string engineVersion = "1.0";

    // boot settings (from Setting/DefaultEngine.ini, or the legacy flat .proj)
    std::string windowTitle  = "Engine";
    int         width        = 1280;
    int         height       = 720;
    EProjectRenderMode renderMode   = EProjectRenderMode::GPU_RT;
    std::string startupWorld;            // empty -> code-hook scene (WorldSetting)

    // Parses `path`. Returns true if the file was opened and read (even if some
    // lines were ignored), false if the file was missing/unreadable (in which
    // case all fields keep their defaults). `path == nullptr` -> false+defaults.
    bool LoadFromFile(const char* path);            // legacy flat Key=Value .proj

    // P7 two-stage boot: .proj manifest (ProjectName/EngineVersion) then the
    // project's Setting/DefaultEngine.ini ([Display]/[Render]/[Startup]).
    bool LoadProject(const char* projPath);
    bool LoadSettings(const char* iniPath);

    static const char* RenderModeName(EProjectRenderMode m);
};
