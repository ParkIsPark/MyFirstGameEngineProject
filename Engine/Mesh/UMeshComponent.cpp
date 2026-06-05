#include "UMeshComponent.h"
#include "UMesh.h"
#include "UMaterial.h"
#include "URay.h"
#include "FArchive.h"

REGISTER_COMPONENT("Mesh", UMeshComponent)

const Material& UMeshComponent::GetMaterial() const
{
    if (const Material* o = EffectiveOverride()) return *o;
    static const Material kDefault;
    return mesh ? mesh->material : kDefault;
}

bool UMeshComponent::intersect(const URay& worldRay,
                               float& outT, int& outTri, float& outU, float& outV) const
{
    if (!mesh) return false;
    return mesh->intersect(worldRay, GetWorldMatrix(), outT, outTri, outU, outV);
}

void UMeshComponent::Serialize(FArchive& ar)
{
    USceneComponent::Serialize(ar);
    ar.Field("Mesh", meshRef);
    ar.Field("Material", materialRef);      // shared material asset (Content/x.material)
    ar.Field("HasMatOverride", hasMaterialOverride);
    if (hasMaterialOverride) materialOverride.Serialize(ar);
    if (ar.IsLoading())
    {
        if (!meshRef.empty() && !mesh)         mesh = UMesh::Resolve(meshRef);          // descriptor / .mesh
        if (!materialRef.empty())              sharedMaterial = UMaterial::Resolve(materialRef);
    }
}
