#include "UMeshComponent.h"
#include "UMesh.h"
#include "URay.h"

bool UMeshComponent::intersect(const URay& worldRay,
                               float& outT, int& outTri, float& outU, float& outV) const
{
    if (!mesh) return false;
    return mesh->intersect(worldRay, GetWorldMatrix(), outT, outTri, outU, outV);
}
