#include "AActor.h"
#include "../Physics/UPrimitiveComponent.h"
#include "../Mesh/UMeshComponent.h"
#include "FArchive.h"
#include <algorithm>
#include <stdexcept>

REGISTER_ACTOR("Actor", AActor)

AActor::AActor()
{
    rootComponent.owner_ = this;
    rootComponent.name  = "Root";
}

AActor::~AActor()
{
    for (const auto& component : components_)
        if (auto* scene = dynamic_cast<USceneComponent*>(component.get())) scene->Detach();
    components_.clear();
    mesh = nullptr;
    physics = nullptr;
}

void AActor::AdoptComponent(UActorComponent* component)
{
    if (!component) return;
    if (component->GetOwner() && component->GetOwner() != this)
        throw std::invalid_argument("Component belongs to a different actor");
    if (component == &rootComponent)
        throw std::invalid_argument("The actor root is not a heap component");
    for (const auto& owned : components_)
        if (owned.get() == component) return;
    // Allocate the slot before taking ownership, so allocation failure leaves
    // the caller's pointer untouched.
    components_.emplace_back();
    components_.back().reset(component);
    component->owner_ = this;
}

void AActor::RemoveOwnedComponent(UActorComponent* component)
{
    auto it = std::find_if(components_.begin(), components_.end(),
        [component](const auto& owned) { return owned.get() == component; });
    if (it == components_.end()) return;
    if (auto* scene = dynamic_cast<USceneComponent*>(component)) scene->Detach();
    components_.erase(it);
}

void AActor::SetMesh(UMeshComponent* m)
{
    AdoptComponent(m);
    if (mesh != m) RemoveOwnedComponent(mesh);
    mesh = m;
    if (m)
    {
        m->AttachTo(&rootComponent);
    }
}

void AActor::SetPhysics(UPrimitiveComponent* p)
{
    AdoptComponent(p);
    if (physics != p) RemoveOwnedComponent(physics);
    physics = p;
}

void AActor::DispatchBeginPlay()
{
    BeginPlay();
    for (const auto& component : components_)
        if (component->IsEnabled()) component->BeginPlay();
}

void AActor::DispatchTick(float deltaSeconds)
{
    Tick(deltaSeconds);
    for (const auto& component : components_)
        if (component->IsEnabled()) component->Tick(deltaSeconds);
}

void AActor::DispatchEndPlay()
{
    for (const auto& component : components_)
        if (component->IsEnabled()) component->EndPlay();
    EndPlay();
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
