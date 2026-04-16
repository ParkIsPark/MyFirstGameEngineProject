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
};
