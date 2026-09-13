#include "UWorld.h"
#include "AActor.h"
#include "../Script/UScriptComponent.h"
#include "../Script/UScriptSubsystem.h"
#include <algorithm>
#include <stdexcept>

namespace
{
    struct DispatchScope
    {
        bool& active;
        explicit DispatchScope(bool& flag) : active(flag) { active = true; }
        ~DispatchScope() { active = false; }
    };
}

UWorld::~UWorld() { EndPlay(); }

void UWorld::RequireMutationAllowed() const
{
    if (dispatching_) throw std::logic_error("Cannot mutate world actors during lifecycle dispatch");
}

void UWorld::SetScriptSubsystem(UScriptSubsystem* subsystem)
{
    RequireMutationAllowed();
    if (playing_) throw std::logic_error("Cannot change script subsystem during Play");
    scripts_ = subsystem;
}

void UWorld::Spawn(AActor* a)
{
    RequireMutationAllowed();
    if (!a || std::find(scene_.Actors.begin(), scene_.Actors.end(), a) != scene_.Actors.end()) return;
    scene_.Actors.push_back(a);
    if (playing_) { DispatchScope scope(dispatching_); BeginActor(a); }
}

bool UWorld::Destroy(AActor* actor)
{
    RequireMutationAllowed();
    const auto found = std::find(scene_.Actors.begin(), scene_.Actors.end(), actor);
    if (found == scene_.Actors.end()) return false;
    if (playing_) { DispatchScope scope(dispatching_); EndActor(actor); }
    scene_.Actors.erase(found);
    delete actor;
    return true;
}

void UWorld::BeginActor(AActor* actor)
{
    for (const auto& component : actor->Components())
        if (auto* script = dynamic_cast<UScriptComponent*>(component.get())) script->ConfigureRuntime(scripts_);
    actor->DispatchBeginPlay(/*propagateFailure=*/false);
}

void UWorld::EndActor(AActor* actor) noexcept
{
    actor->DispatchEndPlay(/*propagateFailure=*/false);
}

void UWorld::BeginPlay()
{
    if (playing_ || dispatching_) return;
    if (scripts_) scripts_->ClearScriptCache();
    playing_ = true;
    DispatchScope scope(dispatching_);
    for (AActor* a : scene_.Actors) BeginActor(a);
}

void UWorld::Tick(float dt)
{
    if (!playing_ || dispatching_) return;
    DispatchScope scope(dispatching_);
    // Physics first (gravity/integration/collision for actors with a primitive),
    // then per-actor game logic. Controllers tick as actors too.
    physics_.Tick(dt, scene_);
    for (AActor* a : scene_.Actors)
        a->DispatchTick(dt, /*propagateFailure=*/false);
}

void UWorld::EndPlay() noexcept
{
    if (!playing_ || dispatching_) return;
    playing_ = false;
    DispatchScope scope(dispatching_);
    for (AActor* a : scene_.Actors) EndActor(a);
}
