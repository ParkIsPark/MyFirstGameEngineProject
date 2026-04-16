#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
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

void ACamera::SetFOV(float fovDeg, float aspect)
{
    fov = fovDeg;
    float halfH = d * std::tan(glm::radians(fovDeg) * 0.5f);
    float halfW = halfH * aspect;
    b = -halfH;  t = halfH;
    l = -halfW;  r = halfW;
}

void ACamera::SetOrientation(float yawDeg, float pitchDeg)
{
    pitchDeg = std::max(-89.0f, std::min(89.0f, pitchDeg));
    yaw   = yawDeg;
    pitch = pitchDeg;

    float cy = std::cos(glm::radians(yawDeg));
    float sy = std::sin(glm::radians(yawDeg));
    float cp = std::cos(glm::radians(pitchDeg));
    float sp = std::sin(glm::radians(pitchDeg));

    // forward = direction the camera looks (into the scene)
    glm::vec3 forward(sy * cp, sp, -cy * cp);
    // right stays horizontal regardless of pitch
    glm::vec3 right(cy, 0.0f, sy);
    // recompute up as cross(right, forward) so the basis is orthonormal
    glm::vec3 up = glm::normalize(glm::cross(right, forward));

    u = right;
    v = up;
    w = -forward;  // camera looks along -w
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
