#include "AActor.h"
#include "../Physics/PhysicalComponent.h"

AActor::AActor()
{
}

void AActor::SetSurface(USurface* s)
{
    surface = s;
    if (s) s->owner = this;
}

void AActor::SetPhysics(PhysicalComponent* p)
{
    physics = p;
    if (p) p->owner = this;
}

AActor::~AActor()
{
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
