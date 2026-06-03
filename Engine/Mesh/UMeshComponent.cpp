#include "UMeshComponent.h"
#include "UMesh.h"
#include "URay.h"
#include "FArchive.h"

REGISTER_COMPONENT("Mesh", UMeshComponent)

bool UMeshComponent::intersect(const URay& worldRay,
                               float& outT, int& outTri, float& outU, float& outV) const
{
    if (!mesh) return false;
    return mesh->intersect(worldRay, GetWorldMatrix(), outT, outTri, outU, outV);
}

void UMeshComponent::Serialize(FArchive& ar)
{
    USceneComponent::Serialize(ar);
    ar.Field("HasMatOverride", hasMaterialOverride);
    if (hasMaterialOverride) materialOverride.Serialize(ar);
    // mesh asset reference (descriptor / .mesh path) lands in P3/P6.
}
