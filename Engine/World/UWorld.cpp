#include "UWorld.h"
#include "AActor.h"
#include "../Script/UScriptComponent.h"
#include "../Script/UScriptSubsystem.h"
#include <algorithm>
#include <stdexcept>
#include <iostream>

namespace
{
    void ReportNativeFailure(const AActor& actor, const char* phase) noexcept
    {
        try
        {
            try { throw; }
            catch (const std::exception& error) { std::cerr << "Native [" << actor.name << "] " << phase << ": " << error.what() << '\n'; }
            catch (...) { std::cerr << "Native [" << actor.name << "] " << phase << ": unknown exception\n"; }
        }
        catch (...) {} // A failing diagnostic stream cannot interrupt cleanup.
    }
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
    try { actor->DispatchBeginPlay(); } catch (...) { ReportNativeFailure(*actor, "BeginPlay"); }
}

void UWorld::EndActor(AActor* actor) noexcept
{
    try { actor->DispatchEndPlay(); } catch (...) { ReportNativeFailure(*actor, "EndPlay"); }
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
        try { a->DispatchTick(dt); } catch (...) { ReportNativeFailure(*a, "Tick"); }
}

void UWorld::EndPlay() noexcept
{
    if (!playing_ || dispatching_) return;
    playing_ = false;
    DispatchScope scope(dispatching_);
    for (AActor* a : scene_.Actors) EndActor(a);
}
