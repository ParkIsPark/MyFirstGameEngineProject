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
    float aspect = static_cast<float>(nx) / static_cast<float>(ny);
    float su = (l + (r - l) * (i + 0.5f) / nx) * aspect;
    float sv =  b + (t - b) * (j + 0.5f) / ny;
    glm::vec3 dir = -d * w + su * u + sv * v;

    URay ray;
    ray.origin    = eye;
    ray.direction = glm::normalize(dir);
    return ray;
}
