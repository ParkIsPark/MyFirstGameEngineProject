#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// USkyHDRI (P-skyhdri) — loads an equirectangular .hdr. GetOrLoad uploads a GL
// float texture for the GPU shading paths' skyColor(); the same float pixels are
// kept on the CPU so the software rasterizer can sample the sky too (SampleDir),
// keeping all render modes consistent. One sky per world; reloads on path change.
// ---------------------------------------------------------------------------
class USkyHDRI
{
public:
    unsigned int GetOrLoad(const std::string& path);   // GL texture id (0 = none)
    void         Cleanup();

    bool      ready() const { return !cpu_.empty(); }  // CPU pixels available
    glm::vec3 SampleDir(const glm::vec3& dir) const;    // equirect lookup (linear RGB)

private:
    std::string        loadedPath_;
    unsigned int       tex_ = 0;
    std::vector<float> cpu_;          // RGB float pixels (CPU copy for the rasterizer)
    int                w_ = 0, h_ = 0;
};
