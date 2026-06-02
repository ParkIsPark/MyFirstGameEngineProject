#include "FProjectDescriptor.h"

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

    ERenderMode parseMode(const std::string& v)
    {
        const std::string m = lower(v);
        if (m == "gpu_rt")     return ERenderMode::GPU_RT;
        if (m == "rasterizer") return ERenderMode::Rasterizer;
        if (m == "hybrid")     return ERenderMode::Hybrid;
        if (m == "cpu_rt")     return ERenderMode::GPU_RT;   // CPU RT removed -> GPU
        return ERenderMode::GPU_RT;                          // unknown -> default
    }
}

const char* FProjectDescriptor::RenderModeName(ERenderMode m)
{
    switch (m)
    {
        case ERenderMode::CPU_RT:     return "CPU_RT";
        case ERenderMode::GPU_RT:     return "GPU_RT";
        case ERenderMode::Rasterizer: return "Rasterizer";
        case ERenderMode::Hybrid:     return "Hybrid";
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
        // unknown keys silently ignored
    }
    return true;
}
