#include "UPhysicsWorld.h"
#include "UPrimitiveComponent.h"
#include "USphereComponent.h"
#include "UBoxComponent.h"
#include "../Core/UScene.h"
#include "../World/AActor.h"

#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static bool isDynamic(const AActor* a)
{
    return a->physics && !a->physics->IsStatic();
}

// Shape parameter accessors — only valid for the matching GetShape() kind.
static float sphereRadius(const UPrimitiveComponent* p)
{
    return static_cast<const USphereComponent*>(p)->radius;
}
static glm::vec3 boxHalf(const UPrimitiveComponent* p)
{
    return static_cast<const UBoxComponent*>(p)->halfExtents;
}

// ---------------------------------------------------------------------------
void UPhysicsWorld::Tick(float dt, UScene& scene)
{
    // ------------------------------------------------------------------
    // 1. Gravity + integration.  isGrounded is reset here and set again
    //    during collision resolution below.
    // ------------------------------------------------------------------
    for (AActor* actor : scene.Actors)
    {
        UPrimitiveComponent* phys = actor->physics;
        if (!phys || phys->IsStatic()) continue;

        if (phys->bAffectedByGravity)
            phys->AddForce(gravity * phys->mass);

        phys->Integrate(dt);
        phys->ClearForces();
        phys->isGrounded = false;
    }

    // ------------------------------------------------------------------
    // 2. Dynamic vs static/dynamic — iterate every ordered pair once.
    //    Collision shape comes from the primitive component (GetShape),
    //    not from the legacy surface.  Actors without a primitive are
    //    skipped (no collider).
    // ------------------------------------------------------------------
    const size_t n = scene.Actors.size();
    for (size_t i = 0; i < n; ++i)
    {
        AActor* a = scene.Actors[i];
        UPrimitiveComponent* pa = a->physics;
        if (!pa) continue;
        const EShape sa = pa->GetShape();

        for (size_t j = i + 1; j < n; ++j)
        {
            AActor* b = scene.Actors[j];
            UPrimitiveComponent* pb = b->physics;
            if (!pb) continue;

            // Both static: no collision response needed
            if (!isDynamic(a) && !isDynamic(b)) continue;

            const EShape sb = pb->GetShape();

            if      (sa == EShape::Sphere && sb == EShape::Sphere)
                resolveSphereSphere(a, sphereRadius(pa), b, sphereRadius(pb));
            else if (sa == EShape::Box && sb == EShape::Box)
                resolveAABBvsAABB  (a, boxHalf(pa), b, boxHalf(pb));
            else if (sa == EShape::Sphere && sb == EShape::Box)
                resolveSphereAABB  (a, sphereRadius(pa), b, boxHalf(pb));
            else if (sa == EShape::Box && sb == EShape::Sphere)
                resolveSphereAABB  (b, sphereRadius(pb), a, boxHalf(pa));
            // (Capsule combinations land in P6.)
        }
    }

    // ------------------------------------------------------------------
    // 3. Dynamic vs floor (a world plane at floorY, not an actor).
    //    Done last so the floor always wins over lateral pushes.
    // ------------------------------------------------------------------
    if (enableFloor)
    {
        for (AActor* actor : scene.Actors)
        {
            if (!isDynamic(actor)) continue;
            UPrimitiveComponent* p = actor->physics;

            if      (p->GetShape() == EShape::Sphere)
                resolveSphereFloor(actor, sphereRadius(p), floorY);
            else if (p->GetShape() == EShape::Box)
                resolveCubeFloor  (actor, boxHalf(p),      floorY);
        }
    }
}

// ---------------------------------------------------------------------------
// applyImpulse
// normal points from b toward a (i.e. a should be pushed in +normal direction)
// ---------------------------------------------------------------------------
void UPhysicsWorld::applyImpulse(AActor* a, AActor* b,
                                  const glm::vec3& normal, float depth,
                                  float restitution)
{
    bool aD = isDynamic(a);
    bool bD = isDynamic(b);

    float massA = aD ? a->physics->mass : 0.0f;
    float massB = bD ? b->physics->mass : 0.0f;
    float total = massA + massB;
    if (total < 1e-6f) return;

    // Position correction (mass-weighted)
    if (aD) a->SetActorLocation(a->GetActorLocation() + normal * depth * (bD ? massB / total : 1.0f));
    if (bD) b->SetActorLocation(b->GetActorLocation() - normal * depth * (aD ? massA / total : 1.0f));

    // isGrounded: if normal has significant upward component, the actor below is
    // being pushed down while the one above lands.  The actor pushed upward is landing.
    if (normal.y > 0.5f && aD)  a->physics->isGrounded = true;
    if (normal.y < -0.5f && bD) b->physics->isGrounded = true;

    // Relative velocity along normal
    glm::vec3 velA = aD ? a->physics->velocity : glm::vec3(0.0f);
    glm::vec3 velB = bD ? b->physics->velocity : glm::vec3(0.0f);
    float vRel = glm::dot(velA - velB, normal);

    if (vRel > 0.0f) return; // already separating

    float invA = aD ? 1.0f / massA : 0.0f;
    float invB = bD ? 1.0f / massB : 0.0f;
    float j = -(1.0f + restitution) * vRel / (invA + invB);

    if (aD) a->physics->velocity += normal * (j * invA);
    if (bD) b->physics->velocity -= normal * (j * invB);
}

// ---------------------------------------------------------------------------
// AABB vs AABB  (SAT on 3 axes, pick minimum penetration axis)
// ---------------------------------------------------------------------------
void UPhysicsWorld::resolveAABBvsAABB(AActor* a, const glm::vec3& halfA,
                                       AActor* b, const glm::vec3& halfB)
{
    glm::vec3 d    = a->physics->WorldCenter() - b->physics->WorldCenter();
    glm::vec3 over = (halfA + halfB) - glm::abs(d);

    if (over.x <= 0.0f || over.y <= 0.0f || over.z <= 0.0f) return;

    // Minimum penetration axis -> collision normal
    glm::vec3 normal;
    float     depth;
    if (over.x < over.y && over.x < over.z)
    {
        depth  = over.x;
        normal = glm::vec3(d.x > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
    }
    else if (over.y < over.z)
    {
        depth  = over.y;
        normal = glm::vec3(0.0f, d.y > 0.0f ? 1.0f : -1.0f, 0.0f);
    }
    else
    {
        depth  = over.z;
        normal = glm::vec3(0.0f, 0.0f, d.z > 0.0f ? 1.0f : -1.0f);
    }

    float e = 0.0f;
    if (isDynamic(a) && isDynamic(b))
        e = glm::min(a->physics->restitution, b->physics->restitution);
    else if (isDynamic(a)) e = a->physics->restitution;
    else                   e = b->physics->restitution;

    applyImpulse(a, b, normal, depth, e);

    // Friction on the tangential velocity components (only when grounded / sliding)
    auto applyFriction = [&](AActor* actor, const glm::vec3& n)
    {
        if (!isDynamic(actor)) return;
        float f = actor->physics->friction;
        glm::vec3& vel = actor->physics->velocity;
        // zero out components perpendicular to normal
        vel -= n * glm::dot(vel, n) * 0.0f; // normal already handled by impulse
        // apply friction to tangential components
        glm::vec3 tang = vel - glm::dot(vel, n) * n;
        vel -= tang * f * 0.016f; // small damping per frame
    };
    applyFriction(a,  normal);
    applyFriction(b, -normal);
}

// ---------------------------------------------------------------------------
// Sphere vs AABB  (closest-point-on-box test)
// ---------------------------------------------------------------------------
void UPhysicsWorld::resolveSphereAABB(AActor* sphere, float radius,
                                       AActor* cube,   const glm::vec3& half)
{
    // Closest point on AABB to sphere center (in world space)
    glm::vec3 local   = sphere->physics->WorldCenter() - cube->physics->WorldCenter();
    glm::vec3 closest = glm::clamp(local, -half, half);
    glm::vec3 diff    = local - closest;
    float     distSq  = glm::dot(diff, diff);

    if (distSq >= radius * radius) return;

    float dist = glm::sqrt(distSq);
    float depth = radius - dist;

    glm::vec3 normal;
    if (dist < 1e-4f)
    {
        // Sphere center is inside the box: push out along shallowest axis
        glm::vec3 over = half - glm::abs(local);
        if (over.x < over.y && over.x < over.z)
        {
            normal = glm::vec3(local.x > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
            depth  = over.x;
        }
        else if (over.y < over.z)
        {
            normal = glm::vec3(0.0f, local.y > 0.0f ? 1.0f : -1.0f, 0.0f);
            depth  = over.y;
        }
        else
        {
            normal = glm::vec3(0.0f, 0.0f, local.z > 0.0f ? 1.0f : -1.0f);
            depth  = over.z;
        }
    }
    else
    {
        normal = diff / dist; // sphere center -> closest point, then invert
    }

    float e = 0.0f;
    if (isDynamic(sphere) && isDynamic(cube))
        e = glm::min(sphere->physics->restitution, cube->physics->restitution);
    else if (isDynamic(sphere)) e = sphere->physics->restitution;
    else                        e = cube->physics->restitution;

    // normal points from cube toward sphere (pushes sphere away)
    applyImpulse(sphere, cube, normal, depth, e);
}

// ---------------------------------------------------------------------------
// Floor helpers (set isGrounded directly, no applyImpulse needed)
// ---------------------------------------------------------------------------
void UPhysicsWorld::resolveSphereFloor(AActor* sphere, float radius, float floorY)
{
    float bottom = sphere->GetActorLocation().y - radius;
    if (bottom >= floorY) return;

    { glm::vec3 p = sphere->GetActorLocation(); p.y = floorY + radius; sphere->SetActorLocation(p); }

    UPrimitiveComponent* phys = sphere->physics;
    if (!phys) return;

    phys->isGrounded = true;
    if (phys->velocity.y < 0.0f)
        phys->velocity.y *= -phys->restitution;

    phys->velocity.x *= (1.0f - phys->friction);
    phys->velocity.z *= (1.0f - phys->friction);
}

void UPhysicsWorld::resolveCubeFloor(AActor* cube, const glm::vec3& half, float floorY)
{
    float bottom = cube->GetActorLocation().y - half.y;
    if (bottom >= floorY) return;

    { glm::vec3 p = cube->GetActorLocation(); p.y = floorY + half.y; cube->SetActorLocation(p); }

    UPrimitiveComponent* phys = cube->physics;
    if (!phys) return;

    phys->isGrounded = true;
    if (phys->velocity.y < 0.0f)
        phys->velocity.y *= -phys->restitution;

    phys->velocity.x *= (1.0f - phys->friction);
    phys->velocity.z *= (1.0f - phys->friction);
}

// ---------------------------------------------------------------------------
// Sphere vs Sphere
// ---------------------------------------------------------------------------
void UPhysicsWorld::resolveSphereSphere(AActor* a, float ra, AActor* b, float rb)
{
    glm::vec3 diff = a->physics->WorldCenter() - b->physics->WorldCenter();
    float dist     = glm::length(diff);
    float minDist  = ra + rb;

    if (dist >= minDist || dist < 1e-4f) return;

    glm::vec3 normal = diff / dist;
    float     depth  = minDist - dist;

    float e = 0.5f;
    if (isDynamic(a) && isDynamic(b))
        e = glm::min(a->physics->restitution, b->physics->restitution);
    else if (isDynamic(a)) e = a->physics->restitution;
    else                   e = b->physics->restitution;

    applyImpulse(a, b, normal, depth, e);
}
