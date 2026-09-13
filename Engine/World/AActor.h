#pragma once
#include <glm/glm.hpp>
#include "USceneComponent.h"
#include <memory>
#include <type_traits>
#include <utility>

class UPrimitiveComponent;
class UMeshComponent;

class AActor
{
public:
    AActor();
    virtual ~AActor();

    // Structural component APIs (AddComponent, AdoptComponent, and typed setters)
    // throw std::logic_error during any lifecycle dispatch, including actor hooks.
    // Rejection occurs before construction, ownership, alias, or hierarchy changes.
    template<class T, class... Args> T& AddComponent(Args&&... args)
    {
        static_assert(std::is_base_of_v<UActorComponent, T>, "T must be an actor component");
        RequireComponentMutationAllowed();
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T& result = *component;
        AdoptComponent(component.get());
        component.release();
        return result;
    }
    template<class T> T* FindComponent() const
    {
        for (const auto& component : components_)
            if (auto* match = dynamic_cast<T*>(component.get())) return match;
        return nullptr;
    }
    const std::vector<std::unique_ptr<UActorComponent>>& Components() const { return components_; }
    // Adopts factory-created heap components; throws before touching a foreign owner.
    void AdoptComponent(UActorComponent* component);

    std::string name;                         // unique within a world (serialization id)

    // The root scene component owns the actor's transform. Other components
    // attach under it (SetMesh / future attachments) to form the scene graph.
    USceneComponent      rootComponent;
    UMeshComponent*      mesh    = nullptr;   // non-owning alias, attached to rootComponent
    UPrimitiveComponent* physics = nullptr;   // non-owning alias: shape + rigid body

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

    // ---- serialization contract (P2) ----
    virtual const char* TypeName() const { return "Actor"; }
    virtual void        Serialize(FArchive& ar);   // name + root transform

    virtual void Tick(float DeltaTime);

    // Public lifecycle drivers so UWorld can dispatch the protected hooks.
    void DispatchBeginPlay();
    void DispatchTick(float deltaSeconds);
    void DispatchEndPlay();

protected:
    void RemoveOwnedComponent(UActorComponent* component);
    virtual void BeginPlay();
    virtual void EndPlay();

private:
    void RequireComponentMutationAllowed() const;
    bool dispatchingComponents_ = false;
    // Declared after rootComponent so heap scene nodes are destroyed before root.
    std::vector<std::unique_ptr<UActorComponent>> components_;
};
