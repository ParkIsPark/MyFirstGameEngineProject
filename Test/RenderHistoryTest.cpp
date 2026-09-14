#include "FRenderHistory.h"
#include "FRenderQuality.h"
#include "FRenderScene.h"
#include "UMesh.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
FRenderScene MakeScene(UMesh& mesh, const std::filesystem::path& texture)
{
    FRenderScene scene;
    scene.camera.eye = {1.0f, 2.0f, 3.0f};
    FResolvedRenderMaterial material;
    material.albedo = {0.2f, 0.4f, 0.8f};
    material.shininess = 23.0f;
    material.runtimeRevision = 17;
    material.diffuseTexturePath = texture.string();
    FRenderMeshInstance instance;
    instance.mesh = &mesh;
    instance.modelTransform[3][0] = 4.0f;
    instance.materialSlots.push_back(material);
    instance.materialSlotIdentities.push_back(1);
    instance.objectIdentity = 7;
    scene.meshes.push_back(instance);
    scene.materialsByIdentity.push_back(material);
    scene.pointLights.push_back({{2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f}});
    scene.environment.skyPath = "test.hdr";
    return scene;
}
}

int main()
{
    const auto texture = std::filesystem::temp_directory_path() / "render-history-texture.tmp";
    { std::ofstream out(texture); out << "a"; }
    UMesh mesh;
    auto scene = MakeScene(mesh, texture);
    FRenderFeatures features;
    features.rayTracing = true;
    FRenderQuality quality;
    const auto signature = [&] {
        return BuildRenderHistorySignature(scene, features, quality, 80, 60,
            ERayTracingBackend::CompatibleGL33, 11, 13);
    };
    const auto original = signature();
    FTemporalSequence sequence;
    const FTemporalFrame first = sequence.Begin(original, 32);
    const FTemporalFrame second = sequence.Begin(original, 32);
    assert(first.reset && first.frameIndex == 0);
    assert(!second.reset && second.frameIndex == 1);
    assert(sequence.Begin(original, 1).frameIndex == 0); // cap changes invalidate history

    auto requireChange = [&](auto mutate) {
        auto copy = scene;
        mutate(copy);
        assert(BuildRenderHistorySignature(copy, features, quality, 80, 60,
            ERayTracingBackend::CompatibleGL33, 11, 13) != original);
    };
    requireChange([](FRenderScene& s) { s.camera.eye.x += 1.0f; });
    requireChange([](FRenderScene& s) { s.camera.fovDegrees += 1.0f; });
    requireChange([](FRenderScene& s) { s.meshes[0].modelTransform[3][1] += 1.0f; });
    requireChange([](FRenderScene& s) { s.materialsByIdentity[0].albedo.x += 0.1f; });
    requireChange([](FRenderScene& s) { ++s.materialsByIdentity[0].runtimeRevision; });
    requireChange([](FRenderScene& s) { s.materialsByIdentity[0].diffuseTexturePath += ".other"; });
    requireChange([](FRenderScene& s) { s.pointLights[0].worldPosition.x += 1.0f; });
    requireChange([](FRenderScene& s) { s.pointLights[0].sourceIntensity.x += 1.0f; });
    requireChange([](FRenderScene& s) { s.environment.tint.x += 0.1f; });
    assert(BuildRenderHistorySignature(scene, FRenderFeatures{}, quality, 80, 60,
        ERayTracingBackend::CompatibleGL33, 11, 13) != signature());
    auto changedQuality = quality; ++changedQuality.shadowSamples;
    assert(BuildRenderHistorySignature(scene, features, changedQuality, 80, 60,
        ERayTracingBackend::CompatibleGL33, 11, 13) != signature());
    assert(BuildRenderHistorySignature(scene, features, quality, 81, 60,
        ERayTracingBackend::CompatibleGL33, 11, 13) != signature());
    assert(BuildRenderHistorySignature(scene, features, quality, 80, 60,
        ERayTracingBackend::ComputeGL43, 11, 13) != signature());
    assert(BuildRenderHistorySignature(scene, features, quality, 80, 60,
        ERayTracingBackend::CompatibleGL33, 12, 13) != signature());
    assert(BuildRenderHistorySignature(scene, features, quality, 80, 60,
        ERayTracingBackend::CompatibleGL33, 11, 14) != signature());
    const auto beforeStamp = signature();
    std::filesystem::last_write_time(texture,
        std::filesystem::last_write_time(texture) + std::chrono::seconds(2));
    assert(signature() != beforeStamp);
    const auto beforeGeometry = signature();
    mesh.MarkGeometryDirty();
    assert(signature() != beforeGeometry);

    sequence.Reset();
    assert(sequence.Begin(original, 32).reset);
    std::filesystem::remove(texture);
    std::cout << "RenderHistoryTest passed\n";
}
