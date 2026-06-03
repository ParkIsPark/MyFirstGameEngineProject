#include "FProjectDescriptor.h"
#include "FIniFile.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace
{
    std::string trim(const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        return s.substr(a, b - a);
    }

    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    EProjectRenderMode parseMode(const std::string& v)
    {
        const std::string m = lower(v);
        if (m == "gpu_rt")     return EProjectRenderMode::GPU_RT;
        if (m == "rasterizer") return EProjectRenderMode::Rasterizer;
        if (m == "hybrid")     return EProjectRenderMode::Hybrid;
        if (m == "cpu_rt")     return EProjectRenderMode::GPU_RT;   // CPU RT removed -> GPU
        return EProjectRenderMode::GPU_RT;                          // unknown -> default
    }
}

const char* FProjectDescriptor::RenderModeName(EProjectRenderMode m)
{
    switch (m)
    {
        case EProjectRenderMode::CPU_RT:     return "CPU_RT";
        case EProjectRenderMode::GPU_RT:     return "GPU_RT";
        case EProjectRenderMode::Rasterizer: return "Rasterizer";
        case EProjectRenderMode::Hybrid:     return "Hybrid";
    }
    return "GPU_RT";
}

bool FProjectDescriptor::LoadFromFile(const char* path)
{
    if (!path || !*path) return false;        // null/empty -> defaults

    std::ifstream file(path);
    if (!file.is_open()) return false;        // missing -> defaults

    std::string line;
    while (std::getline(file, line))
    {
        // Strip comments (# ...) and blank lines.
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;            // not a key=value line

        const std::string key = lower(trim(line.substr(0, eq)));
        const std::string val = trim(line.substr(eq + 1));
        if (key.empty()) continue;

        if      (key == "windowtitle")  { if (!val.empty()) windowTitle = val; }
        else if (key == "width")        { try { int w = std::stoi(val); if (w > 0) width  = w; } catch (...) {} }
        else if (key == "height")       { try { int h = std::stoi(val); if (h > 0) height = h; } catch (...) {} }
        else if (key == "rendermode")   { if (!val.empty()) renderMode = parseMode(val); }
        else if (key == "startupworld") { startupWorld = val; }
        else if (key == "projectname")  { if (!val.empty()) projectName = val; }
        else if (key == "engineversion"){ if (!val.empty()) engineVersion = val; }
        // unknown keys silently ignored
    }
    return true;
}

// .proj manifest: flat ProjectName / EngineVersion (no sections).
bool FProjectDescriptor::LoadProject(const char* projPath)
{
    FIniFile ini;
    if (!ini.LoadFromFile(projPath)) return false;
    projectName   = ini.GetString("", "ProjectName",   projectName);
    engineVersion = ini.GetString("", "EngineVersion", engineVersion);
    return true;
}

// Project boot settings: Setting/DefaultEngine.ini [Display]/[Render]/[Startup].
bool FProjectDescriptor::LoadSettings(const char* iniPath)
{
    FIniFile ini;
    if (!ini.LoadFromFile(iniPath)) return false;
    windowTitle  = ini.GetString("Display", "Title",  windowTitle);
    width        = ini.GetInt   ("Display", "Width",  width);
    height       = ini.GetInt   ("Display", "Height", height);
    if (ini.Has("Render", "Mode"))      renderMode   = parseMode(ini.GetString("Render", "Mode"));
    if (ini.Has("Startup", "DefaultWorld")) startupWorld = ini.GetString("Startup", "DefaultWorld");
    return true;
}
