#pragma once
#include <glm/glm.hpp>
#include "USurface.h"   // Material (per-instance override)

class UMesh;
class AActor;
struct URay;

// ---------------------------------------------------------------------------
// UMeshComponent — per-actor mesh instance (Unreal StaticMeshComponent analogue).
//
// Points at a shared UMesh asset and carries the per-instance data: a relative
// transform (offset from the owning actor) and an optional material override.
// World transform = M_actor(owner T*R*S) * M_relative(T*R*S).
//
// It is a thin wrapper: world<->local conversion + dispatch into UMesh.
// ---------------------------------------------------------------------------
class UMeshComponent
{
public:
    UMesh* mesh = nullptr;             // shared asset

    // per-instance relative transform (relative to the owner actor)
    glm::vec3 relPosition = glm::vec3(0.0f);
    glm::vec3 relRotation = glm::vec3(0.0f); // Euler XYZ (degrees)
    glm::vec3 relScale    = glm::vec3(1.0f);

    // per-instance material override (applied over mesh->material)
    Material materialOverride;
    bool     hasMaterialOverride = false;

    // world = M_actor(owner) * M_relative(this)
    glm::mat4 GetWorldMatrix(const AActor& owner) const;

    // World-space ray -> mesh-local (via GetWorldMatrix) -> UMesh::intersect.
    bool intersect(const URay& worldRay, const AActor& owner,
                   float& outT, int& outTri, float& outU, float& outV) const;
};
