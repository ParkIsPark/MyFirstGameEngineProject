#include "AActor.h"
#include "../Physics/UPrimitiveComponent.h"
#include "../Mesh/UMeshComponent.h"
#include "FArchive.h"

REGISTER_ACTOR("Actor", AActor)

AActor::AActor()
{
    rootComponent.owner = this;
    rootComponent.name  = "Root";
}

AActor::~AActor()
{
    // Owns its heap components (allocated via new in SetMesh/SetPhysics, the
    // factory loader, and CloneActor's deep copy). Without this, every world
    // switch / undo eviction / NewWorld leaked the mesh + physics components
    // (incl. a textured material's CPU pixel bytes). rootComponent is a value
    // member; UMeshComponent never deletes the shared UMesh, so no double free.
    delete mesh;     mesh    = nullptr;
    delete physics;  physics = nullptr;
}

void AActor::SetMesh(UMeshComponent* m)
{
    mesh = m;
    if (m)
    {
        m->owner        = this;
        m->attachParent = nullptr;   // clear any parent copied during a deep copy
        m->children.clear();
        m->AttachTo(&rootComponent);
    }
}

void AActor::SetPhysics(UPrimitiveComponent* p)
{
    physics = p;
    if (p) p->owner = this;
}

void AActor::Serialize(FArchive& ar)
{
    ar.Field("Name", name);
    glm::vec3 loc = rootComponent.relLocation;
    glm::vec3 rot = rootComponent.relRotation;
    glm::vec3 scl = rootComponent.relScale;
    ar.Field("Loc",   loc);
    ar.Field("Rot",   rot);
    ar.Field("Scale", scl);
    if (ar.IsLoading())
    {
        rootComponent.relLocation = loc;
        rootComponent.relRotation = rot;
        rootComponent.relScale    = scl;
        rootComponent.MarkDirty();
    }
}

void AActor::Tick(float DeltaTime)
{
}

void AActor::BeginPlay()
{
}

void AActor::EndPlay()
{
}
