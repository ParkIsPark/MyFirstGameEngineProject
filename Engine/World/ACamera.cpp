#include <glm/glm.hpp>
#include "ACamera.h"
#include "URay.h"

ACamera::ACamera()
    : eye(0.0f, 0.0f, 0.0f)
    , u(1.0f, 0.0f, 0.0f)
    , v(0.0f, 1.0f, 0.0f)
    , w(0.0f, 0.0f, 1.0f)
    , l(-0.1f), r(0.1f), b(-0.1f), t(0.1f), d(0.1f)
{
}

URay ACamera::generateRay(int i, int j, int nx, int ny) const
{
    return generateRayJittered(i, j, nx, ny, 0.5f, 0.5f);
}

URay ACamera::generateRayJittered(int i, int j, int nx, int ny, float jx, float jy) const
{
    float aspect = static_cast<float>(nx) / static_cast<float>(ny);
    float su = (l + (r - l) * (i + jx) / nx) * aspect;
    float sv =  b + (t - b) * (j + jy) / ny;
    glm::vec3 dir = -d * w + su * u + sv * v;

    URay ray;
    ray.origin    = eye;
    ray.direction = glm::normalize(dir);
    return ray;
}
