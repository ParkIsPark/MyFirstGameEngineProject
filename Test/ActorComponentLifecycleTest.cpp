// Build/run from the repository root (PowerShell):
// & C:\msys64\ucrt64\bin\g++.exe -std=c++17 -DGLM_FORCE_RADIANS -Iinclude -IEngine/World -IEngine/Mesh -IEngine/Physics -IEngine/Light -IEngine/Serialization -IEngine/Core -IEngine/RayTracing Test/ActorComponentLifecycleTest.cpp Engine/World/UActorComponent.cpp Engine/World/AActor.cpp Engine/World/USceneComponent.cpp Engine/Physics/UPrimitiveComponent.cpp Engine/Physics/UBoxComponent.cpp Engine/Light/ALight.cpp Engine/Light/LightComponent.cpp Engine/Serialization/FArchive.cpp Engine/World/UWorld.cpp Engine/World/ACamera.cpp Engine/Core/UScene.cpp Engine/Physics/UPhysicsWorld.cpp Engine/Physics/USphereComponent.cpp -o actor_component_test.exe
// if ($LASTEXITCODE -eq 0) { .\actor_component_test.exe }
#include "UActorComponent.h"
#include "AActor.h"
#include "UBoxComponent.h"
#include "ALight.h"
#include "UWorld.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using Events = std::vector<std::string>;

struct Recorder : UActorComponent {
    Events& events;
    std::string id;
    Recorder(Events& events, std::string id) : events(events), id(std::move(id)) {
        events.push_back(this->id + " constructed");
    }
    ~Recorder() override { events.push_back(id + " destroyed"); }
    std::string_view TypeName() const override { return "Recorder"; }
    void BeginPlay() override { events.push_back(id + " begin"); }
    void Tick(float dt) override { assert(dt == 0.25f); events.push_back(id + " tick"); }
    void EndPlay() override { events.push_back(id + " end"); }
};
struct DerivedRecorder : Recorder { using Recorder::Recorder; };
struct RecordingActor : AActor {
    Events& events;
    explicit RecordingActor(Events& events) : events(events) {}
    void BeginPlay() override { events.push_back("actor begin"); }
    void Tick(float dt) override { assert(dt == 0.25f); events.push_back("actor tick"); }
    void EndPlay() override { events.push_back("actor end"); }
};
struct CountedBox : UBoxComponent {
    int& destroyed;
    explicit CountedBox(int& destroyed) : destroyed(destroyed) {}
    ~CountedBox() override { ++destroyed; }
};
struct CountedLight : LightComponent {
    int& destroyed;
    explicit CountedLight(int& destroyed) : destroyed(destroyed) {}
    ~CountedLight() override { ++destroyed; }
};

// Production breaks caught: missing adoption/owner assignment, wrong insertion
// order, disabled dispatch, wrong actor/component ordering, double destruction,
// failed foreign-owner rejection, and broken typed lookup. Expectations below
// are literal event sequences and counts, independent of production helpers.
int main() {
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_ALWAYS_DF | _CRTDBG_LEAK_CHECK_DF);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
#endif
    Events events;
    {
        RecordingActor actor(events);
        assert(actor.rootComponent.GetOwner() == &actor);
        assert(actor.Components().empty()); // value root is never heap-owned
        auto& first = actor.AddComponent<DerivedRecorder>(events, "first");
        auto& disabled = actor.AddComponent<Recorder>(events, "disabled");
        auto& last = actor.AddComponent<Recorder>(events, "last");
        assert(first.GetOwner() == &actor && last.GetOwner() == &actor);
        assert(first.IsEnabled());
        disabled.SetEnabled(false);
        assert(!disabled.IsEnabled());
        assert(actor.Components().size() == 3);
        assert(actor.Components()[0].get() == &first);
        assert(actor.Components()[1].get() == &disabled);
        assert(actor.Components()[2].get() == &last);
        assert(actor.FindComponent<Recorder>() == &first);
        assert(actor.FindComponent<DerivedRecorder>() == &first);
        assert(actor.FindComponent<USceneComponent>() == nullptr);
        actor.DispatchBeginPlay();
        actor.DispatchTick(0.25f);
        actor.DispatchEndPlay();
        assert((events == Events{"first constructed", "disabled constructed", "last constructed",
            "actor begin", "first begin", "last begin", "actor tick", "first tick", "last tick",
            "first end", "last end", "actor end"}));
        disabled.SetEnabled(true);
        events.clear();
        actor.DispatchTick(0.25f);
        assert((events == Events{"actor tick", "first tick", "disabled tick", "last tick"}));
    }
    for (const auto* id : {"first destroyed", "disabled destroyed", "last destroyed"})
        assert(std::count(events.begin(), events.end(), id) == 1);

    int oldDestroyed = 0, newDestroyed = 0;
    {
        AActor actor, foreign;
        auto* old = new CountedBox(oldDestroyed);
        actor.SetPhysics(old);
        assert(old->GetOwner() == &actor && actor.physics == old);
        bool rejected = false;
        try { foreign.SetPhysics(old); } catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected && foreign.physics == nullptr && old->GetOwner() == &actor);
        assert(oldDestroyed == 0 && actor.Components().size() == 1 && foreign.Components().empty());
        actor.SetPhysics(old); // repeated adoption cannot duplicate ownership
        assert(actor.Components().size() == 1 && oldDestroyed == 0);
        auto* replacement = new CountedBox(newDestroyed);
        actor.SetPhysics(replacement);
        assert(oldDestroyed == 1 && actor.physics == replacement && actor.Components().size() == 1);
        actor.SetActorLocation({1, 2, 3});
        replacement->velocity = {4, 0, 0};
        replacement->Integrate(0.5f);
        assert(actor.GetActorLocation().x == 3);
        actor.SetPhysics(nullptr);
        assert(newDestroyed == 1 && actor.physics == nullptr && actor.Components().empty());
    }
    assert(oldDestroyed == 1 && newDestroyed == 1);
    int lightDestroyed = 0;
    {
        ALight actor(new CountedLight(lightDestroyed));
        assert(actor.lightComp->GetOwner() == &actor);
        assert(actor.lightComp->attachParent == &actor.rootComponent);
        assert(actor.Components().size() == 1);
        actor.SetActorLocation({5, 0, 0});
        assert(actor.lightComp->GetWorldLocation().x == 5);
        bool rejected = false;
        try { ALight foreign(actor.lightComp); } catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected && lightDestroyed == 0 && actor.lightComp->GetOwner() == &actor);
    }
    assert(lightDestroyed == 1);

    // Copying an editor component must not copy ownership or hierarchy links;
    // otherwise adoption rejects the clone or corrupts the source scene graph.
    {
        ALight source(new LightComponent);
        source.SetActorLocation({10, 0, 0});
        source.lightComp->SetRelativeLocation({2, 0, 0});
        source.lightComp->SetEnabled(false);
        USceneComponent child;
        child.AttachTo(source.lightComp);
        assert(source.lightComp->GetWorldLocation().x == 12);
        auto copy = std::make_unique<LightComponent>(*source.lightComp);
        assert(copy->GetOwner() == nullptr && copy->attachParent == nullptr && copy->children.empty());
        assert(!copy->IsEnabled() && copy->GetWorldLocation().x == 2);
        ALight clone(copy.release());
        assert(clone.lightComp->GetOwner() == &clone && source.rootComponent.children.size() == 1);
        assert(child.attachParent == source.lightComp);
        *clone.lightComp = *source.lightComp;
        assert(clone.lightComp->GetOwner() == &clone && clone.lightComp->attachParent == &clone.rootComponent);
        source.SetLightComponent(nullptr);
        assert(child.attachParent == nullptr);
        assert(child.GetWorldLocation().x == 0);
    }

    // Deleting a scene parent must invalidate a surviving child's world cache.
    {
        USceneComponent child;
        {
            AActor actor;
            auto& parent = actor.AddComponent<USceneComponent>();
            parent.SetRelativeLocation({9, 0, 0});
            child.AttachTo(&parent);
            assert(child.GetWorldLocation().x == 9);
        }
        assert(child.attachParent == nullptr && child.GetWorldLocation().x == 0);
    }
    // A direct virtual Tick call in UWorld silently skips component scripts.
    events.clear();
    {
        UWorld world;
        auto* actor = new RecordingActor(events);
        actor->AddComponent<Recorder>(events, "world component");
        world.Spawn(actor);
        world.BeginPlay();
        world.Tick(0.25f);
        world.EndPlay();
        assert((events == Events{"world component constructed", "actor begin", "world component begin",
            "actor tick", "world component tick", "world component end", "actor end"}));
    }
    assert(events.back() == "world component destroyed");
#if defined(_MSC_VER) && defined(_DEBUG)
    assert(_CrtCheckMemory());
#endif
    std::cout << "actor component lifecycle: PASS\n";
}
