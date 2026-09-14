// Current complete recipe: powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name MaterialOpticsTest
#include "AActor.h"
#include "FArchive.h"
#include "FRenderOutputs.h"
#include "FRenderMath.h"
#include "FRenderScene.h"
#include "Material.h"
#include "UMaterial.h"
#include "UMesh.h"
#include "UMeshComponent.h"
#include "UWorld.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

namespace
{
bool Near(float lhs, float rhs) { return std::fabs(lhs - rhs) < 1.0e-6f; }
bool Near(const glm::vec3& lhs, const glm::vec3& rhs)
{
    return Near(lhs.x, rhs.x) && Near(lhs.y, rhs.y) && Near(lhs.z, rhs.z);
}
}

int main()
{
    // Mutations caught: additive mirrors, glass double-counting local energy,
    // or ray-backend failure leaving a surface black/invisible.
    const auto ordinary = ResolveMaterialOpticalWeights(false, 1.0f, 0.0f, 1.0f, true);
    assert(Near(ordinary.local, 1.0f) && Near(ordinary.mirror, 0.0f) &&
           Near(ordinary.transmission, 0.0f));
    const auto halfMirror = ResolveMaterialOpticalWeights(false, 1.0f, 0.5f, 1.0f, true);
    assert(Near(halfMirror.local, 0.5f) && Near(halfMirror.mirror, 0.5f) &&
           Near(halfMirror.transmission, 0.0f));
    const auto fullMirror = ResolveMaterialOpticalWeights(false, 1.0f, 1.0f, 1.0f, true);
    assert(Near(fullMirror.local, 0.0f) && Near(fullMirror.mirror, 1.0f) &&
           Near(fullMirror.transmission, 0.0f));
    const auto fullGlass = ResolveMaterialOpticalWeights(true, 0.0f, 1.0f, 1.0f, true);
    assert(Near(fullGlass.local, 0.0f) && Near(fullGlass.mirror, 0.0f) &&
           Near(fullGlass.transmission, 1.0f));
    const auto mixed = ResolveMaterialOpticalWeights(true, 0.25f, 0.5f, 1.0f, true);
    assert(Near(mixed.local, 0.125f) && Near(mixed.mirror, 0.125f) &&
           Near(mixed.transmission, 0.75f));
    const auto fallback = ResolveMaterialOpticalWeights(true, 0.0f, 1.0f, 1.0f, false);
    assert(Near(fallback.local, 1.0f) && Near(fallback.mirror, 0.0f) &&
           Near(fallback.transmission, 0.0f));

    // Mutations caught: treating UV/normal seam duplicates as boundary edges,
    // or accepting a genuinely open indexed surface as a dielectric volume.
    std::unique_ptr<UMesh> closedCube(UMesh::GenerateCube(glm::vec3(1.0f)));
    assert(IsClosedTriangleMesh(*closedCube));
    std::unique_ptr<UMesh> epsilonSeamCube(UMesh::GenerateCube(glm::vec3(1.0f)));
    bool perturbedSeamDuplicate = false;
    for (std::size_t i = 0; i < epsilonSeamCube->vertices.size() && !perturbedSeamDuplicate; ++i)
        for (std::size_t j = i + 1; j < epsilonSeamCube->vertices.size(); ++j)
            if (epsilonSeamCube->vertices[i].position == epsilonSeamCube->vertices[j].position)
            {
                epsilonSeamCube->vertices[j].position.x += 0.75e-5f;
                perturbedSeamDuplicate = true;
                break;
            }
    if (!perturbedSeamDuplicate || !IsClosedTriangleMesh(*epsilonSeamCube))
    {
        std::fprintf(stderr, "epsilon seam canonicalization failed (duplicate=%d)\n",
                     perturbedSeamDuplicate ? 1 : 0);
        return 1;
    }
    closedCube->indices.resize(closedCube->indices.size() - 3u);
    closedCube->MarkGeometryDirty();
    assert(!IsClosedTriangleMesh(*closedCube));

    // Mutations caught: accepting unknown blend tokens or widening/narrowing
    // any author-facing optical clamp.
    Material invalid;
    FLoadArchive invalidArchive(
        "blendMode = Additive\nopacity = -2\nrefraction = 9\n"
        "transmittanceColor = 0 -1 4\ntransmittanceDistance = 0\n");
    invalid.Serialize(invalidArchive);
    assert(invalid.blendMode == EMaterialBlendMode::Opaque);
    assert(Near(invalid.opacity, 0.0f));
    assert(Near(invalid.refraction, 2.42f));
    assert(Near(invalid.transmittanceColor, {0.0001f, 0.0001f, 1.0f}));
    assert(Near(invalid.transmittanceDistance, 0.0001f));

    Material upperLower;
    upperLower.opacity = 2.0f;
    upperLower.refraction = 0.5f;
    upperLower.transmittanceColor = {2.0f, 0.5f, 0.00001f};
    upperLower.transmittanceDistance = -3.0f;
    upperLower.SanitizeOptics();
    assert(Near(upperLower.opacity, 1.0f));
    assert(Near(upperLower.refraction, 1.0f));
    assert(Near(upperLower.transmittanceColor, {1.0f, 0.5f, 0.0001f}));
    assert(Near(upperLower.transmittanceDistance, 0.0001f));

    // Mutation caught: per-frame extraction using unsanitized source values or
    // omitting one of the clear-glass fields from its scalar-only snapshot.
    UWorld world;
    auto* actor = new AActor();
    auto* component = new UMeshComponent();
    component->mesh = UMesh::GenerateCube(glm::vec3(1.0f));
    component->hasMaterialOverride = true;
    component->materialOverride.blendMode = EMaterialBlendMode::Translucent;
    component->materialOverride.opacity = -1.0f;
    component->materialOverride.refraction = 3.0f;
    component->materialOverride.transmittanceColor = {0.8f, 0.0f, 1.4f};
    component->materialOverride.transmittanceDistance = 0.0f;
    component->materialOverride.castRayTracedShadows = false;
    const std::uint64_t sourceRevision = component->materialOverride.RuntimeRevision();
    actor->SetMesh(component);
    world.Spawn(actor);
    const FRenderScene scene = ExtractRenderScene(world, world.GetCamera());
    assert(scene.meshes.size() == 1 && scene.meshes[0].materialOverride.has_value());
    const FResolvedRenderMaterial& resolved = *scene.meshes[0].materialOverride;
    assert(resolved.blendMode == EMaterialBlendMode::Translucent);
    assert(Near(resolved.opacity, 0.0f) && Near(resolved.refraction, 2.42f));
    assert(Near(resolved.transmittanceColor, {0.8f, 0.0001f, 1.0f}));
    assert(Near(resolved.transmittanceDistance, 0.0001f));
    assert(!resolved.castRayTracedShadows && resolved.runtimeRevision == sourceRevision);
    assert(resolved.source == &component->materialOverride && resolved.source->texData.empty());

    FLogicalGBufferSample logical;
    assert(logical.blendMode == EMaterialBlendMode::Opaque && Near(logical.opacity, 1.0f));
    assert(Near(logical.refraction, 1.52f) && Near(logical.transmittanceColor, glm::vec3(1.0f)));
    assert(Near(logical.transmittanceDistance, 1.0f) && logical.castRayTracedShadows);

    // Mutations caught: serializing the runtime-only revision or failing to
    // invalidate material/texture reload consumers.
    Material reloaded;
    const std::uint64_t beforeMaterialLoad = reloaded.RuntimeRevision();
    FLoadArchive materialReload("opacity = 0.5\n");
    reloaded.Serialize(materialReload);
    assert(reloaded.RuntimeRevision() > beforeMaterialLoad);
    FSaveArchive serialized;
    reloaded.Serialize(serialized);
    assert(serialized.str().find("Revision") == std::string::npos &&
           serialized.str().find("revision") == std::string::npos);
    const std::uint64_t beforeTextureReload = reloaded.RuntimeRevision();
    UMaterial::LoadTexture(reloaded);
    assert(reloaded.RuntimeRevision() > beforeTextureReload);

    std::remove("material_optics_format2.tmp.material");
    Material authored;
    authored.blendMode = EMaterialBlendMode::Translucent;
    authored.opacity = 0.1f;
    authored.refraction = 1.33f;
    authored.transmittanceColor = {0.9f, 0.7f, 0.5f};
    authored.transmittanceDistance = 4.0f;
    authored.castRayTracedShadows = false;
    assert(UMaterial::Save("material_optics_format2.tmp.material", authored));
    std::ifstream file("material_optics_format2.tmp.material");
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    file.close();
    assert(text.find("MaterialFormat = 2") == 0);
    Material* loaded = UMaterial::Resolve("material_optics_format2.tmp.material");
    assert(loaded && loaded->blendMode == authored.blendMode && Near(loaded->opacity, authored.opacity));
    assert(Near(loaded->refraction, authored.refraction) &&
           Near(loaded->transmittanceColor, authored.transmittanceColor));
    assert(Near(loaded->transmittanceDistance, authored.transmittanceDistance) &&
           loaded->castRayTracedShadows == authored.castRayTracedShadows);
    std::remove("material_optics_format2.tmp.material");

    std::puts("MaterialOpticsTest passed");
    return 0;
}
