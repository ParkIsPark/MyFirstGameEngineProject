#include "AActor.h"
#include "../Physics/UPrimitiveComponent.h"
#include "../Mesh/UMeshComponent.h"

AActor::AActor()
{
    rootComponent.owner = this;
    rootComponent.name  = "Root";
}

AActor::~AActor()
{
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

void AActor::Tick(float DeltaTime)
{
}

void AActor::BeginPlay()
{
}

void AActor::EndPlay()
{
}
