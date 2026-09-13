// & 'C:\Program Files\Git\bin\bash.exe' --noprofile --norc -c 'export PATH=/c/msys64/ucrt64/bin:$PATH; g++ -std=c++17 -ffunction-sections -fdata-sections -Wall -Wextra -pedantic -I./include -I./Engine/Render -I./Engine/Mesh -I./Engine/Acceleration -I./Engine/RayTracing -I./Engine/Import Test/GPUMeshCacheTest.cpp Engine/Render/UGPUMeshCache.cpp Engine/Mesh/UMesh.cpp Engine/Acceleration/BVH.cpp Engine/Import/UObjImporter.cpp -Wl,--gc-sections -o GPUMeshCacheTest.exe && ./GPUMeshCacheTest.exe'
#include "UGPUMeshCache.h"

#include "UMesh.h"
#include "UObjImporter.h"
#include "UFbxImporter.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>

// The pure cache test links UMesh without the external Assimp binary. This
// narrow link seam is never exercised here; the real FBX importer is compiled
// and linked by the Visual Studio integration builds below.
std::vector<UMesh*> UFbxImporter::Load(const char*)
{
    return {};
}

namespace
{
class FFakeMeshUploadAdapter final : public IMeshGPUUploadAdapter
{
public:
    explicit FFakeMeshUploadAdapter(std::uint64_t activeGeneration)
        : activeGeneration(activeGeneration) {}

    std::uint64_t ActiveContextGeneration() const noexcept override
    {
        return activeGeneration;
    }

    FGPUMeshResource CreateAndUpload(const FMeshGPUUploadView& upload,
                                     std::uint64_t contextGeneration) override
    {
        Validate(upload);
        assert(contextGeneration == activeGeneration);
        FGPUMeshResource resource;
        resource.vao = nextHandle++;
        resource.vertexBuffer = nextHandle++;
        resource.indexBuffer = nextHandle++;
        resource.contextGeneration = returnedGenerationOverride
            ? returnedGenerationOverride : contextGeneration;
        assert(liveHandles.insert(resource.vao).second);
        assert(liveHandles.insert(resource.vertexBuffer).second);
        assert(liveHandles.insert(resource.indexBuffer).second);
        ++createCalls;
        events.push_back('C');
        return resource;
    }

    bool Reupload(FGPUMeshResource& resource,
                  const FMeshGPUUploadView& upload,
                  std::string& diagnostic) noexcept override
    {
        Validate(upload);
        assert(liveHandles.count(resource.vao) == 1);
        assert(liveHandles.count(resource.vertexBuffer) == 1);
        assert(liveHandles.count(resource.indexBuffer) == 1);
        ++reuploadCalls;
        events.push_back('R');
        if (failNextReupload)
        {
            failNextReupload = false;
            diagnostic = "injected reupload failure";
            return false;
        }
        diagnostic.clear();
        return true;
    }

    void Destroy(FGPUMeshResource& resource) noexcept override
    {
        assert(resource.vao != 0);
        assert(liveHandles.erase(resource.vao) == 1);
        assert(liveHandles.erase(resource.vertexBuffer) == 1);
        assert(liveHandles.erase(resource.indexBuffer) == 1);
        resource = {};
        ++destroyCalls;
        events.push_back('D');
    }

    void Abandon(FGPUMeshResource& resource) noexcept override
    {
        assert(resource.vao != 0);
        assert(liveHandles.erase(resource.vao) == 1);
        assert(liveHandles.erase(resource.vertexBuffer) == 1);
        assert(liveHandles.erase(resource.indexBuffer) == 1);
        resource = {};
        ++abandonCalls;
        events.push_back('A');
    }

    std::size_t createCalls = 0;
    std::size_t reuploadCalls = 0;
    std::size_t destroyCalls = 0;
    std::size_t abandonCalls = 0;
    bool failNextReupload = false;
    std::uint64_t activeGeneration = 0;
    std::uint64_t returnedGenerationOverride = 0;
    std::unordered_set<unsigned> liveHandles;
    std::string events;

private:
    static void Validate(const FMeshGPUUploadView& upload)
    {
        assert(upload.vertexCount == 0 || upload.vertices != nullptr);
        assert(upload.indexCount == 0 || upload.indices != nullptr);
    }

    unsigned nextHandle = 1;
};

void PopulateTriangle(UMesh& mesh)
{
    mesh.vertices = {
        {{0, 0, 0}, {0, 0, 1}, {0, 0}},
        {{1, 0, 0}, {0, 0, 1}, {1, 0}},
        {{0, 1, 0}, {0, 0, 1}, {0, 1}},
    };
    mesh.indices = {0, 1, 2};
    mesh.FinalizeGeometry();
}

void CheckMeshIdentityAndRevisionContract()
{
    UMesh first;
    UMesh second;
    assert(first.AssetId() != 0);
    assert(second.AssetId() != 0);
    assert(first.AssetId() != second.AssetId());

    const FMeshAssetId stableId = first.AssetId();
    const std::uint64_t initialRevision = first.GeometryRevision();
    assert(initialRevision != 0);

    first.vertices.push_back({});
    first.indices.push_back(0);
    assert(first.AssetId() == stableId);
    assert(first.GeometryRevision() == initialRevision);

    first.FinalizeGeometry();
    assert(first.GeometryRevision() == initialRevision + 1);
    const std::uint64_t finalizedRevision = first.GeometryRevision();

    first.material.shininess = 17.0f;
    assert(first.GeometryRevision() == finalizedRevision);

    first.BuildBVH();
    assert(first.bvh);
    assert(first.GeometryRevision() == finalizedRevision);

    first.MarkGeometryDirty();
    assert(!first.bvh);
    assert(first.GeometryRevision() == finalizedRevision + 1);
}

void CheckBuiltInCompletionHooks()
{
    const std::uint64_t initialRevision = UMesh().GeometryRevision();

    UMesh* sphere = UMesh::GenerateSphere(1.0f, 8, 6);
    UMesh* cube = UMesh::GenerateCube(glm::vec3(1.0f));
    UMesh* plane = UMesh::GeneratePlane(glm::vec2(2.0f));
    assert(sphere && sphere->GeometryRevision() == initialRevision + 1);
    assert(cube && cube->GeometryRevision() == initialRevision + 1);
    assert(plane && plane->GeometryRevision() == initialRevision + 1);

    const char* meshPath = "GPUMeshCacheTest.tmp.mesh";
    assert(cube->SaveBinary(meshPath));
    UMesh* loaded = UMesh::LoadBinary(meshPath);
    assert(loaded && loaded->GeometryRevision() == initialRevision + 1);

    UMesh* merged = UMesh::MergeWithSlots({cube, plane});
    assert(merged && merged->GeometryRevision() == initialRevision + 1);

    const char* objPath = "GPUMeshCacheTest.tmp.obj";
    {
        std::ofstream obj(objPath);
        obj << "v 0 0 0\n"
               "v 1 0 0\n"
               "v 0 1 0\n"
               "usemtl first\n"
               "f 1 2 3\n"
               "usemtl second\n"
               "f 1 3 2\n";
    }
    UMesh* objSingle = UObjImporter::Load(objPath);
    std::vector<UMesh*> objParts = UObjImporter::LoadMulti(objPath);
    assert(objSingle && objSingle->GeometryRevision() == initialRevision + 1);
    assert(objParts.size() == 2);
    for (UMesh* part : objParts)
        assert(part && part->GeometryRevision() == initialRevision + 1);

    const char* emptyObjPath = "GPUMeshCacheTest.empty.tmp.obj";
    { std::ofstream emptyObj(emptyObjPath); }
    UMesh* emptyObj = UObjImporter::Load(emptyObjPath);
    assert(emptyObj && emptyObj->GeometryRevision() == initialRevision);

    delete sphere;
    delete cube;
    delete plane;
    delete loaded;
    delete merged;
    delete objSingle;
    delete emptyObj;
    for (UMesh* part : objParts) delete part;
    std::remove(meshPath);
    std::remove(objPath);
    std::remove(emptyObjPath);
}

void CheckAcquireAndRevisionUploads()
{
    FFakeMeshUploadAdapter adapter(7);
    {
        UGPUMeshCache cache(adapter);
        UMesh mesh;
        PopulateTriangle(mesh);

        const FGPUMeshResource& first = cache.Acquire(mesh, 7);
        assert(first.vao != 0);
        assert(first.uploadedRevision == mesh.GeometryRevision());
        assert(first.indexCount == 3);
        assert(cache.Stats().residentResources == 1);
        assert(cache.Stats().uploads == 1);
        assert(cache.Stats().reuploads == 0);
        assert(cache.Stats().releases == 0);

        const unsigned firstVao = first.vao;
        assert(cache.Acquire(mesh, 7).vao == firstVao);
        assert(cache.Stats().uploads == 1);

        const std::uint64_t unchangedRevision = mesh.GeometryRevision();
        mesh.vertices[0].position.x = 4.0f;
        mesh.material.shininess = 32.0f;
        glm::mat4 transformOnly(1.0f);
        transformOnly[3][0] = 9.0f;
        assert(transformOnly[3][0] == 9.0f);
        assert(mesh.GeometryRevision() == unchangedRevision);
        cache.Acquire(mesh, 7);
        assert(cache.Stats().reuploads == 0);

        mesh.BuildBVH();
        mesh.MarkGeometryDirty();
        assert(!mesh.bvh);
        cache.Acquire(mesh, 7);
        assert(cache.Stats().reuploads == 1);

        mesh.indices.push_back(999999u);
        mesh.FinalizeGeometry();
        const FGPUMeshResource& refreshed = cache.Acquire(mesh, 7);
        assert(refreshed.indexCount == 4);
        assert(cache.Stats().reuploads == 2);

        UMesh distinct;
        PopulateTriangle(distinct);
        assert(distinct.AssetId() != mesh.AssetId());
        cache.Acquire(distinct, 7);
        assert(cache.Stats().residentResources == 2);
        assert(cache.Stats().uploads == 2);

        UMesh empty;
        const FGPUMeshResource& emptyResource = cache.Acquire(empty, 7);
        assert(emptyResource.indexCount == 0);
        assert(cache.Stats().residentResources == 3);
        assert(cache.Stats().uploads == 3);
    }
    assert(adapter.createCalls == 3);
    assert(adapter.reuploadCalls == 2);
    assert(adapter.destroyCalls == 3);
    assert(adapter.liveHandles.empty());
}

void CheckFrameReleasePolicy()
{
    FFakeMeshUploadAdapter adapter(1);
    {
        UGPUMeshCache cache(adapter);
        UMesh kept;
        UMesh unused;
        PopulateTriangle(kept);
        PopulateTriangle(unused);

        cache.BeginFrame();
        cache.Acquire(kept, 1);
        cache.Acquire(unused, 1);
        cache.ReleaseUnused();
        assert(cache.Stats().residentResources == 2);
        assert(cache.Stats().releases == 0);

        cache.BeginFrame();
        cache.Acquire(kept, 1);
        cache.Acquire(kept, 1);
        cache.ReleaseUnused();
        assert(cache.Stats().residentResources == 1);
        assert(cache.Stats().releases == 1);
        cache.ReleaseUnused();
        assert(cache.Stats().residentResources == 1);
        assert(cache.Stats().releases == 1);
    }
    assert(adapter.destroyCalls == 2);
    assert(adapter.liveHandles.empty());
}

void CheckFailedReuploadRetainsMetadataAndRetries()
{
    FFakeMeshUploadAdapter adapter(4);
    UGPUMeshCache cache(adapter);
    UMesh mesh;
    PopulateTriangle(mesh);

    const FGPUMeshResource& initial = cache.Acquire(mesh, 4);
    const std::uint64_t uploadedRevision = initial.uploadedRevision;
    const std::size_t uploadedIndexCount = initial.indexCount;
    mesh.indices.insert(mesh.indices.end(), {0u, 2u, 1u});
    mesh.FinalizeGeometry();

    adapter.failNextReupload = true;
    bool threw = false;
    try { cache.Acquire(mesh, 4); }
    catch (const std::runtime_error& error)
    {
        threw = std::string(error.what()).find("injected reupload failure") != std::string::npos;
    }
    assert(threw);
    assert(initial.uploadedRevision == uploadedRevision);
    assert(initial.indexCount == uploadedIndexCount);
    assert(cache.Stats().reuploads == 0);
    assert(cache.Stats().failedReuploads == 1);

    const FGPUMeshResource& retried = cache.Acquire(mesh, 4);
    assert(retried.uploadedRevision == mesh.GeometryRevision());
    assert(retried.indexCount == mesh.indices.size());
    assert(cache.Stats().reuploads == 1);
    assert(cache.Stats().failedReuploads == 1);
}

void CheckContextInvalidationAndClearStats()
{
    FFakeMeshUploadAdapter adapter(5);
    {
        UGPUMeshCache cache(adapter);
        UMesh first;
        UMesh second;
        PopulateTriangle(first);
        PopulateTriangle(second);

        cache.Acquire(first, 5);
        cache.Acquire(second, 5);
        assert(cache.Stats().uploads == 2);
        assert(cache.Stats().residentResources == 2);

        adapter.activeGeneration = 12;
        cache.Acquire(first, 12);
        assert(adapter.events == "CCAAC");
        assert(cache.Stats().uploads == 3);
        assert(cache.Stats().reuploads == 0);
        assert(cache.Stats().releases == 0);
        assert(cache.Stats().abandons == 2);
        assert(cache.Stats().residentResources == 1);

        cache.Clear();
        assert(cache.Stats().releases == 1);
        assert(cache.Stats().residentResources == 0);
        cache.Clear();
        assert(cache.Stats().releases == 1);

        cache.Acquire(second, 12);
        assert(cache.Stats().uploads == 4);
        assert(cache.Stats().residentResources == 1);
    }
    assert(adapter.createCalls == 4);
    assert(adapter.reuploadCalls == 0);
    assert(adapter.destroyCalls == 2);
    assert(adapter.abandonCalls == 2);
    assert(adapter.liveHandles.empty());
}

void CheckGenerationAuthorityRejectsBeforeMutation()
{
    FFakeMeshUploadAdapter adapter(7);
    UGPUMeshCache cache(adapter);
    UMesh mesh;
    PopulateTriangle(mesh);

    bool mismatchRejected = false;
    try { cache.Acquire(mesh, 8); }
    catch (const std::invalid_argument&) { mismatchRejected = true; }
    assert(mismatchRejected);
    assert(adapter.events.empty());
    assert(cache.Stats().residentResources == 0);

    cache.Acquire(mesh, 7);
    adapter.activeGeneration = 9;
    mismatchRejected = false;
    try { cache.Acquire(mesh, 8); }
    catch (const std::invalid_argument&) { mismatchRejected = true; }
    assert(mismatchRejected);
    assert(adapter.events == "C");
    assert(cache.Stats().abandons == 0);
    assert(cache.Stats().residentResources == 1);

    cache.Acquire(mesh, 9);
    assert(adapter.events == "CAC");
    assert(cache.Stats().abandons == 1);
}

void CheckCreatedGenerationLabelIsValidated()
{
    FFakeMeshUploadAdapter adapter(3);
    adapter.returnedGenerationOverride = 99;
    UGPUMeshCache cache(adapter);
    UMesh mesh;
    PopulateTriangle(mesh);

    bool rejected = false;
    try { cache.Acquire(mesh, 3); }
    catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    assert(adapter.events == "CA");
    assert(cache.Stats().residentResources == 0);
    assert(cache.Stats().uploads == 0);
}
} // namespace

int main()
{
    CheckMeshIdentityAndRevisionContract();
    CheckBuiltInCompletionHooks();
    CheckAcquireAndRevisionUploads();
    CheckFrameReleasePolicy();
    CheckFailedReuploadRetainsMetadataAndRetries();
    CheckContextInvalidationAndClearStats();
    CheckGenerationAuthorityRejectsBeforeMutation();
    CheckCreatedGenerationLabelIsValidated();
    std::cout << "GPUMeshCacheTest passed\n";
    return 0;
}
