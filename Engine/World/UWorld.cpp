#include "UWorld.h"
#include "AActor.h"

void UWorld::Spawn(AActor* a)
{
    if (a) scene_.Actors.push_back(a);
}

void UWorld::BeginPlay()
{
    for (AActor* a : scene_.Actors)
        a->DispatchBeginPlay();
}

void UWorld::Tick(float dt)
{
    // Physics first (gravity/integration/collision for actors with a primitive),
    // then per-actor game logic. Controllers tick as actors too.
    physics_.Tick(dt, scene_);
    for (AActor* a : scene_.Actors)
        a->Tick(dt);
}

void UWorld::EndPlay()
{
    for (AActor* a : scene_.Actors)
        a->DispatchEndPlay();
}
