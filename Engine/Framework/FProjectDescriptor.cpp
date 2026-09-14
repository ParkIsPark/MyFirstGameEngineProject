#include "FProjectDescriptor.h"
#include "FIniFile.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>

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

    bool parseBool(const std::string& value, bool& result)
    {
        const std::string v = lower(trim(value));
        if (v == "1" || v == "true" || v == "yes" || v == "on") { result = true; return true; }
        if (v == "0" || v == "false" || v == "no" || v == "off") { result = false; return true; }
        return false;
    }

    bool parseBackend(const std::string& value, ERayTracingBackend& result)
    {
        const std::string v = lower(trim(value));
        if (v == "auto")       { result = ERayTracingBackend::Auto; return true; }
        if (v == "compatible") { result = ERayTracingBackend::CompatibleGL33; return true; }
        if (v == "compute")    { result = ERayTracingBackend::ComputeGL43; return true; }
        return false;
    }

    bool applyLegacyMode(const std::string& value, FRenderFeatures& features)
    {
        const std::string mode = lower(trim(value));
        bool rayTracing = false;
        if (mode == "rasterizer") rayTracing = false;
        else if (mode == "gpu_rt" || mode == "hybrid" || mode == "cpu_rt") rayTracing = true;
        else return false;

        features.hardwareRaster = true;
        features.rayTracing = rayTracing;
        features.rayTracedShadows = true;
        features.rayTracedGI = true;
        features.rayTracedReflections = true;
        features.rayTracedTranslucency = true;
        features.rayTracingBackend = ERayTracingBackend::Auto;
        return true;
    }

    void applyNamedFeatures(const FIniFile& ini, const std::string& section, FRenderFeatures& features)
    {
        bool value = false;
        if (ini.Has(section, "HardwareRaster") && parseBool(ini.GetString(section, "HardwareRaster"), value))
            features.hardwareRaster = true; // mandatory, even when a project writes 0
        if (ini.Has(section, "RayTracing") && parseBool(ini.GetString(section, "RayTracing"), value))
            features.rayTracing = value;
        if (ini.Has(section, "RayTracedShadows") && parseBool(ini.GetString(section, "RayTracedShadows"), value))
            features.rayTracedShadows = value;
        if (ini.Has(section, "RayTracedGI") && parseBool(ini.GetString(section, "RayTracedGI"), value))
            features.rayTracedGI = value;
        if (ini.Has(section, "RayTracedReflections") && parseBool(ini.GetString(section, "RayTracedReflections"), value))
            features.rayTracedReflections = value;
        if (ini.Has(section, "RayTracedTranslucency") && parseBool(ini.GetString(section, "RayTracedTranslucency"), value))
            features.rayTracedTranslucency = value;
        if (ini.Has(section, "RayTracingBackend"))
        {
            ERayTracingBackend backend;
            if (parseBackend(ini.GetString(section, "RayTracingBackend"), backend))
                features.rayTracingBackend = backend;
            else {
                features.rayTracingBackend = ERayTracingBackend::Auto;
                std::cerr << "[Project] Unknown RayTracingBackend '"
                    << ini.GetString(section, "RayTracingBackend") << "'; using Auto\n";
            }
        }
        features.hardwareRaster = true;
    }
}

const char* FProjectDescriptor::RayTracingBackendName(ERayTracingBackend backend)
{
    switch (backend)
    {
        case ERayTracingBackend::Auto:           return "Auto";
        case ERayTracingBackend::CompatibleGL33: return "Compatible";
        case ERayTracingBackend::ComputeGL43:    return "Compute";
    }
    return "Auto";
}

bool FProjectDescriptor::LoadFromFile(const char* path)
{
    if (!path || !*path) return false;        // null/empty -> defaults

    std::ifstream file(path);
    if (!file.is_open()) return false;        // missing -> defaults

    std::string line;
    std::string contents;
    while (std::getline(file, line))
    {
        contents += line + "\n";
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
        else if (key == "rendermode")   { if (!val.empty()) applyLegacyMode(val, defaultRenderFeatures); }
        else if (key == "startupworld") { startupWorld = val; }
        else if (key == "projectname")  { if (!val.empty()) projectName = val; }
        else if (key == "engineversion"){ if (!val.empty()) engineVersion = val; }
        // unknown keys silently ignored
    }
    FIniFile named;
    named.Parse(contents);
    applyNamedFeatures(named, "", defaultRenderFeatures);
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
    if (ini.Has("Render", "Mode")) applyLegacyMode(ini.GetString("Render", "Mode"), defaultRenderFeatures);
    applyNamedFeatures(ini, "Render", defaultRenderFeatures); // named fields win over Mode
    if (ini.Has("Startup", "DefaultWorld")) startupWorld = ini.GetString("Startup", "DefaultWorld");
    return true;
}
