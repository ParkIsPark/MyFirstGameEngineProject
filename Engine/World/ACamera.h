#pragma once
#include "URay.h"
#include <glm/glm.hpp>

class ACamera
{
public:
    // --- View-plane frustum (used directly by ray generation) ---
    glm::vec3 eye;
    glm::vec3 u, v, w;         // right, up, -forward axes (camera looks along -w)
    float l, r, b, t, d;       // left/right/bottom/top/near-plane offsets

    // --- Orientation angles (degrees) ---
    float yaw   = 0.0f;        // horizontal rotation around world Y  (0 = look into -Z)
    float pitch = 0.0f;        // vertical tilt  (-89..89 clamped)

    // --- Field of view ---
    float fov   = 60.0f;       // vertical FOV in degrees

    ACamera();

    // Recompute l/r/b/t from fov + aspect ratio (width/height).
    // Call whenever fov or window aspect changes.
    void SetFOV(float fovDeg, float aspect);

    // Recompute u/v/w from yaw and pitch angles (degrees).
    // pitch is clamped to [-89, 89] to avoid gimbal flip.
    void SetOrientation(float yawDeg, float pitchDeg);

    // Generate a ray through the center of pixel (i, j).
    // Camera looks along -w.
    URay generateRay(int i, int j, int nx, int ny) const;
};
