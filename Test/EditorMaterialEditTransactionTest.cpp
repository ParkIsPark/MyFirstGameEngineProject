// Current complete recipe:
// powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name EditorMaterialEditTransactionTest
#include "FEditorAssetWorkflow.h"
#include "FWorldSerializer.h"
#include "AActor.h"
#include "Material.h"
#include "UMesh.h"
#include "UMeshComponent.h"
#include "UWorld.h"

#include <cassert>
#include <memory>
#include <stdexcept>
#include <string>

int main()
{
    UMesh* sharedMesh = UMesh::Resolve("Cube");
    assert(sharedMesh);
    const glm::vec3 sharedColor = sharedMesh->material.kd;

    UWorld world;
    auto* actor = new AActor();
    auto* component = new UMeshComponent();
    component->mesh = sharedMesh;
    component->meshRef = "Cube";
    actor->SetMesh(component);
    world.Spawn(actor);

    FEditorMaterialDraft edited = MakeEditorMaterialDraft(component->GetMaterial());
    edited.kd = {0.2f, 0.4f, 0.8f};
    edited.blendMode = EMaterialBlendMode::Translucent;
    edited.opacity = -2.0f;
    const std::uint64_t priorRevision = component->materialOverride.RuntimeRevision();
    std::string undoState;
    int undoSnapshots = 0;
    CommitEditorComponentMaterialEdit(*component, edited, false, [&]
    {
        ++undoSnapshots;
        assert(!component->hasMaterialOverride);
        assert(sharedMesh->material.kd == sharedColor);
        undoState = FWorldSerializer::Save(world);
    });

    assert(undoSnapshots == 1 && component->hasMaterialOverride);
    assert(component->materialOverride.kd == edited.kd);
    assert(component->materialOverride.opacity == 0.0f);
    assert(component->materialOverride.RuntimeRevision() > priorRevision);
    assert(sharedMesh->material.kd == sharedColor);
    const std::string redoState = FWorldSerializer::Save(world);

    // The same serialized snapshots used by Editor undo/redo must restore the
    // pre-edit no-override state and the committed component-local edit.
    std::unique_ptr<UWorld> undone(FWorldSerializer::Load(undoState));
    std::unique_ptr<UWorld> redone(FWorldSerializer::Load(redoState));
    UMeshComponent* undoneComponent = undone && !undone->GetScene().Actors.empty()
        ? undone->GetScene().Actors.front()->mesh : nullptr;
    UMeshComponent* redoneComponent = redone && !redone->GetScene().Actors.empty()
        ? redone->GetScene().Actors.front()->mesh : nullptr;
    assert(undoneComponent && !undoneComponent->hasMaterialOverride);
    assert(redoneComponent && redoneComponent->hasMaterialOverride);
    assert(redoneComponent->materialOverride.kd == edited.kd);
    assert(redoneComponent->materialOverride.blendMode == EMaterialBlendMode::Translucent);
    assert(redoneComponent->materialOverride.opacity == 0.0f);
    assert(sharedMesh->material.kd == sharedColor);

    // A failed pre-change snapshot leaves both the override flag and value intact.
    FEditorMaterialDraft rejected = MakeEditorMaterialDraft(component->materialOverride);
    rejected.kd = {1.0f, 0.0f, 0.0f};
    bool threw = false;
    try
    {
        CommitEditorComponentMaterialEdit(*component, rejected, false, []
        {
            throw std::runtime_error("snapshot failed");
        });
    }
    catch (const std::runtime_error&) { threw = true; }
    assert(threw && component->hasMaterialOverride);
    assert(component->materialOverride.kd == edited.kd);
    assert(sharedMesh->material.kd == sharedColor);

    // The production UI creates a payload-free draft every frame. Merely drawing
    // the inspector and committing a scalar field retain the resident texture.
    Material resident;
    resident.diffuseTexPath = "Content/Textures/large.png";
    resident.texWidth = 4096;
    resident.texHeight = 4096;
    resident.texChannels = 4;
    resident.texture = 77;
    resident.texData.resize(4096u * 4096u * 4u, 123u);
    const unsigned char* const payloadAddress = resident.texData.data();
    const size_t payloadCapacity = resident.texData.capacity();

    FEditorMaterialDraft noChange = MakeEditorMaterialDraft(resident);
    assert(resident.texData.data() == payloadAddress);
    assert(resident.texData.capacity() == payloadCapacity);
    assert(resident.texData.front() == 123u && resident.texData.back() == 123u);

    noChange.shininess = 91.0f;
    const std::uint64_t residentRevision = resident.RuntimeRevision();
    CommitEditorMaterialEdit(resident, noChange, false, [&]
    {
        assert(resident.shininess != 91.0f);
        assert(resident.texData.data() == payloadAddress);
    });
    assert(resident.shininess == 91.0f);
    assert(resident.RuntimeRevision() > residentRevision);
    assert(resident.texData.data() == payloadAddress);
    assert(resident.texData.capacity() == payloadCapacity);
    assert(resident.texWidth == 4096 && resident.texHeight == 4096 && resident.texChannels == 4);
    assert(resident.texture == 77);
    assert(resident.texData.front() == 123u && resident.texData.back() == 123u);
    return 0;
}
