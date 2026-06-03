#pragma once
#include <glm/glm.hpp>
#include "USceneComponent.h"   // base: relative transform + scene-graph attachment
#include "Material.h"          // per-instance material override

class UMesh;
struct URay;

// ---------------------------------------------------------------------------
// UMeshComponent — per-actor mesh instance (Unreal StaticMeshComponent analogue).
//
// A USceneComponent that points at a shared UMesh asset. Its relative transform
// (relLocation/relRotation/relScale, inherited) is the offset from its attach
// parent (the owning actor's root component); the world transform comes from
// USceneComponent::GetWorldMatrix() walking the attachment chain.
// ---------------------------------------------------------------------------
class UMeshComponent : public USceneComponent
{
public:
    UMesh* mesh = nullptr;             // shared asset (resolved from meshRef on load)
    std::string meshRef;               // descriptor ("Sphere 2 32 16") or "Content/x.mesh"

    // per-instance material override (applied over mesh->material)
    Material materialOverride;
    bool     hasMaterialOverride = false;

    // World-space ray -> mesh-local (via GetWorldMatrix) -> UMesh::intersect.
    bool intersect(const URay& worldRay,
                   float& outT, int& outTri, float& outU, float& outV) const;

    const char* TypeName() const override { return "Mesh"; }
    void        Serialize(FArchive& ar) override;   // base + material override
};
