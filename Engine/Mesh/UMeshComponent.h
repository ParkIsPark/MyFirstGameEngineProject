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

    // Shared material ASSET (Content/x.material). When set, it wins over the
    // override/mesh material; it is a SHARED pointer from UMaterial::Resolve, so
    // editing it changes every component that references the same path.
    std::string materialRef;
    Material*   sharedMaterial = nullptr;   // resolved from materialRef (not owned)

    // The single material to render this mesh with, or nullptr to fall back to the
    // mesh's own per-triangle slots. Precedence: shared asset > override > slots.
    const Material* EffectiveOverride() const
    { return sharedMaterial ? sharedMaterial : (hasMaterialOverride ? &materialOverride : nullptr); }
    // Convenience whole-mesh material (shared > override > mesh default).
    const Material& GetMaterial() const;

    // World-space ray -> mesh-local (via GetWorldMatrix) -> UMesh::intersect.
    bool intersect(const URay& worldRay,
                   float& outT, int& outTri, float& outU, float& outV) const;

    const char* TypeName() const override { return "Mesh"; }
    void        Serialize(FArchive& ar) override;   // base + material override
};
