#pragma once
#include "URay.h"
#include <glm/glm.hpp>

class ACamera
{
public:
    glm::vec3 eye;
    glm::vec3 u, v, w;
    float l, r, b, t, d;

    ACamera();

    // Generate a ray through the center of pixel (i, j).
    // Camera looks along -w.
    URay generateRay(int i, int j, int nx, int ny) const;

    // Generate a ray with sub-pixel jitter (jx, jy) in [0,1).
    // Used for anti-aliasing: replaces the fixed 0.5 center offset.
    URay generateRayJittered(int i, int j, int nx, int ny, float jx, float jy) const;
};
