#pragma once
#include <string>

// ---------------------------------------------------------------------------
// USkyHDRI (P-skyhdri) — loads an equirectangular .hdr into a GL float texture
// for the GPU shading paths' skyColor() (background + hemisphere ambient).
// One sky per world; GetOrLoad reloads only when the path changes. Returns 0 on
// failure / empty path (renderer falls back to the procedural gradient).
// ---------------------------------------------------------------------------
class USkyHDRI
{
public:
    unsigned int GetOrLoad(const std::string& path);   // GL texture id (0 = none)
    void         Cleanup();

private:
    std::string  loadedPath_;
    unsigned int tex_ = 0;
};
