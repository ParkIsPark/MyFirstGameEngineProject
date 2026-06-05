#pragma once
#include <glm/glm.hpp>

class AActor;

// Collision shape kind, used by UPhysicsWorld's narrowphase dispatch.
enum class EShape { Sphere, Box, Capsule };

// ---------------------------------------------------------------------------
// UPrimitiveComponent — Unreal-style physics/collision base.
//
// Combines rigid-body dynamics (mass/velocity/force/restitution/friction/
// gravity) with an *analytic collision-shape* interface. Promoted from the old
// PhysicalComponent (identical dynamics members) so physics no longer reads the
// render mesh (UMesh) or the legacy USurface -- the collision shape now lives in
// the component itself. Concrete shapes derive via UShapeComponent.
//
// Principle: collision shape != render mesh. One component = shape + body.
// ---------------------------------------------------------------------------
class UPrimitiveComponent
{
public:
    // --- rigid-body dynamics (moved verbatim from PhysicalComponent) ---
    float mass        = 1.0f;   // 0 = static object (immovable)
    float restitution = 0.5f;   // bounciness: 0=inelastic, 1=perfectly elastic
    float friction    = 0.3f;   // surface friction coefficient

    bool  bAffectedByGravity = true;

    // Simulate Physics (Unreal-style). false => STATIC: the body still collides
    // but never moves (immovable / infinite mass, no gravity). Use it for ground
    // planes and walls so dynamic bodies rest on them instead of pushing them.
    bool  bSimulate = true;

    glm::vec3 velocity   = glm::vec3(0.0f);
    glm::vec3 force      = glm::vec3(0.0f); // accumulated per frame
    bool      isGrounded = false;           // set by UPhysicsWorld on contact

    AActor* owner = nullptr;

    // Collider's local position offset from the owning actor (its own transform;
    // shape "size" is halfExtents / radius). World center = actor loc + localOffset.
    glm::vec3 localOffset = glm::vec3(0.0f);

    UPrimitiveComponent() = default;
    explicit UPrimitiveComponent(AActor* owner) : owner(owner) {}
    virtual ~UPrimitiveComponent() = default;

    // Static when physics simulation is off or mass is zero: collides but never moves.
    bool IsStatic() const { return !bSimulate || mass <= 0.0f; }

    // World-space collider center (actor location + localOffset). In the .cpp
    // where AActor is complete.
    glm::vec3 WorldCenter() const;

    void AddForce(const glm::vec3& f);
    void Integrate(float dt);
    void ClearForces();

    // --- analytic collision interface (implemented by shape subclasses) ---
    virtual EShape GetShape() const = 0;

    // World-space axis-aligned bounding box for broadphase culling.
    // Implemented in each shape's .cpp (needs the full AActor definition).
    virtual void GetWorldAABB(glm::vec3& outMin, glm::vec3& outMax) const = 0;
};
