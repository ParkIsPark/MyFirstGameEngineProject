#pragma once
#include "UScene.h"
#include "ACamera.h"
#include "../Physics/UPhysicsWorld.h"

class AActor;

// ---------------------------------------------------------------------------
// UWorld (E4) — runtime scene container (Unreal UWorld analogue).
//
// Owns the scene (actors/lights/output), the active camera, and the physics
// world, and drives the actor lifecycle: BeginPlay() once, then Tick() every
// frame (physics step -> actor ticks), then EndPlay() at teardown. The Engine
// only spins the loop; all simulation responsibility lives here.
//
// WorldSetting() (an Engine hook) constructs a UWorld and Spawn()s the actors.
// ---------------------------------------------------------------------------
class UWorld
{
public:
    void Spawn(AActor* a);     // add an actor (UScene takes ownership / deletes it)

    void BeginPlay();          // dispatch BeginPlay() to every actor once
    void Tick(float dt);       // physics.Tick(dt) -> each actor->Tick(dt)
    void EndPlay();            // dispatch EndPlay() to every actor

    UScene&        GetScene()   { return scene_; }
    ACamera&       GetCamera()  { return camera_; }
    UPhysicsWorld& GetPhysics() { return physics_; }

private:
    UScene        scene_;
    ACamera       camera_;
    UPhysicsWorld physics_;
};
