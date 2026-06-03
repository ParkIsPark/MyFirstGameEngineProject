#pragma once
#include <glm/glm.hpp>
#include "USceneComponent.h"

class UPrimitiveComponent;
class UMeshComponent;

class AActor
{
public:
    AActor();
    virtual ~AActor();

    // The root scene component owns the actor's transform. Other components
    // attach under it (SetMesh / future attachments) to form the scene graph.
    USceneComponent      rootComponent;
    UMeshComponent*      mesh    = nullptr;   // mesh instance, attached to rootComponent
    UPrimitiveComponent* physics = nullptr;   // root primitive: shape + rigid body

    // ---- transform accessors (delegate to rootComponent; all writes MarkDirty) ----
    glm::vec3 GetActorLocation() const { return rootComponent.GetWorldLocation(); }
    void      SetActorLocation(const glm::vec3& v) { rootComponent.relLocation = v; rootComponent.MarkDirty(); }
    glm::vec3 GetActorRotation() const { return rootComponent.relRotation; }
    void      SetActorRotation(const glm::vec3& v) { rootComponent.relRotation = v; rootComponent.MarkDirty(); }
    glm::vec3 GetActorScale()    const { return rootComponent.relScale; }
    void      SetActorScale(const glm::vec3& v)    { rootComponent.relScale = v; rootComponent.MarkDirty(); }
    glm::vec3 GetPosition()      const { return GetActorLocation(); } // back-compat alias

    // Sets the mesh instance and attaches it under the root component.
    void SetMesh(UMeshComponent* m);
    // Sets the physics component and wires the owner back-pointer.
    void SetPhysics(UPrimitiveComponent* p);

    virtual void Tick(float DeltaTime);

    // Public lifecycle drivers so UWorld can dispatch the protected hooks.
    void DispatchBeginPlay() { BeginPlay(); }
    void DispatchEndPlay()   { EndPlay(); }

protected:
    virtual void BeginPlay();
    virtual void EndPlay();
};
