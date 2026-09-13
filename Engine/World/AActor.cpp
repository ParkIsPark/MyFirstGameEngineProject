#include "AActor.h"
#include "../Physics/UPrimitiveComponent.h"
#include "../Mesh/UMeshComponent.h"
#include "FArchive.h"
#include <algorithm>
#include <stdexcept>
#include <exception>

REGISTER_ACTOR("Actor", AActor)

namespace
{
    // Restore the prior state on normal return and exception unwinding, including
    // nested dispatches that must leave the outer dispatch protected.
    class ComponentDispatchScope
    {
    public:
        explicit ComponentDispatchScope(bool& active) : active_(active), previous_(active)
        { active_ = true; }
        ~ComponentDispatchScope() { active_ = previous_; }
        ComponentDispatchScope(const ComponentDispatchScope&) = delete;
        ComponentDispatchScope& operator=(const ComponentDispatchScope&) = delete;
    private:
        bool& active_;
        bool previous_;
    };
}

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
    RequireComponentMutationAllowed();
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
    RequireComponentMutationAllowed();
    auto it = std::find_if(components_.begin(), components_.end(),
        [component](const auto& owned) { return owned.get() == component; });
    if (it == components_.end()) return;
    if (auto* scene = dynamic_cast<USceneComponent*>(component)) scene->Detach();
    components_.erase(it);
}

void AActor::SetMesh(UMeshComponent* m)
{
    // Reserve before adoption: AttachTo then cannot allocate after the actor
    // becomes owner, so an allocation failure leaves the caller's component
    // untouched and avoids ambiguous ownership during unwinding.
    if (m && m->attachParent != &rootComponent)
        rootComponent.ReserveChildAttachment();
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
    ComponentDispatchScope dispatchScope(dispatchingComponents_);
    std::exception_ptr failure;
    nativeTickFailed_ = false;
    try { BeginPlay(); }
    catch (...) { nativeTickFailed_ = true; failure = std::current_exception(); }
    for (const auto& component : components_)
    {
        component->nativeTickFailed_ = false;
        if (component->IsEnabled())
            try { component->BeginPlay(); }
            catch (...) { component->nativeTickFailed_ = true; if (!failure) failure = std::current_exception(); }
    }
    if (failure) std::rethrow_exception(failure);
}

void AActor::DispatchTick(float deltaSeconds)
{
    ComponentDispatchScope dispatchScope(dispatchingComponents_);
    std::exception_ptr failure;
    if (!nativeTickFailed_)
        try { Tick(deltaSeconds); }
        catch (...) { nativeTickFailed_ = true; failure = std::current_exception(); }
    for (const auto& component : components_)
        if (component->IsEnabled() && !component->nativeTickFailed_)
            try { component->Tick(deltaSeconds); }
            catch (...) { component->nativeTickFailed_ = true; if (!failure) failure = std::current_exception(); }
    if (failure) std::rethrow_exception(failure);
}

void AActor::DispatchEndPlay()
{
    ComponentDispatchScope dispatchScope(dispatchingComponents_);
    std::exception_ptr failure;
    for (const auto& component : components_)
        if (component->IsEnabled() || component->RequiresEndPlay())
            try { component->EndPlay(); }
            catch (...) { if (!failure) failure = std::current_exception(); }
    try { EndPlay(); } catch (...) { if (!failure) failure = std::current_exception(); }
    if (failure) std::rethrow_exception(failure);
}

void AActor::RequireComponentMutationAllowed() const
{
    if (dispatchingComponents_)
        throw std::logic_error("Cannot change actor components during lifecycle dispatch");
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
