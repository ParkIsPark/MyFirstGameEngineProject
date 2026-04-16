#include "UPhysicsWorld.h"
#include "PhysicalComponent.h"
#include "../Core/UScene.h"
#include "../World/AActor.h"
#include "../World/SphereSurface.h"
#include "../World/CubeSurface.h"
#include "../World/PlaneSurface.h"

#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static bool isDynamic(const AActor* a)
{
    return a->physics && !a->physics->IsStatic();
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
        PhysicalComponent* phys = actor->physics;
        if (!phys || phys->IsStatic()) continue;

        if (phys->bAffectedByGravity)
            phys->AddForce(gravity * phys->mass);

        phys->Integrate(dt);
        phys->ClearForces();
        phys->isGrounded = false;
    }

    // ------------------------------------------------------------------
    // 2. Dynamic vs static/dynamic — iterate every ordered pair once.
    // ------------------------------------------------------------------
    const size_t n = scene.Actors.size();
    for (size_t i = 0; i < n; ++i)
    {
        AActor* a = scene.Actors[i];
        SphereSurface* sa = dynamic_cast<SphereSurface*>(a->surface);
        CubeSurface*   ca = dynamic_cast<CubeSurface*>  (a->surface);

        for (size_t j = i + 1; j < n; ++j)
        {
            AActor* b = scene.Actors[j];

            // Both static: no collision response needed
            if (!isDynamic(a) && !isDynamic(b)) continue;

            // Skip plane-plane or plane-something handled separately
            if (dynamic_cast<PlaneSurface*>(a->surface) ||
                dynamic_cast<PlaneSurface*>(b->surface)) continue;

            SphereSurface* sb = dynamic_cast<SphereSurface*>(b->surface);
            CubeSurface*   cb = dynamic_cast<CubeSurface*>  (b->surface);

            if      (sa && sb) resolveSphereSphere(a, sa->radius, b, sb->radius);
            else if (ca && cb) resolveAABBvsAABB  (a, ca->halfVec, b, cb->halfVec);
            else if (sa && cb) resolveSphereAABB  (a, sa->radius,  b, cb->halfVec);
            else if (ca && sb) resolveSphereAABB  (b, sb->radius,  a, ca->halfVec);
        }
    }

    // ------------------------------------------------------------------
    // 3. Dynamic vs horizontal planes (floor).
    //    Done last so floor always wins over lateral pushes.
    // ------------------------------------------------------------------
    for (AActor* actor : scene.Actors)
    {
        if (!isDynamic(actor)) continue;

        SphereSurface* ss = dynamic_cast<SphereSurface*>(actor->surface);
        CubeSurface*   cs = dynamic_cast<CubeSurface*>  (actor->surface);

        for (AActor* plane : scene.Actors)
        {
            if (!dynamic_cast<PlaneSurface*>(plane->surface)) continue;

            if (ss) resolveSphereFloor(actor, ss->radius,  plane->position.y);
            if (cs) resolveCubeFloor  (actor, cs->halfVec, plane->position.y);
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
    if (aD) a->position += normal * depth * (bD ? massB / total : 1.0f);
    if (bD) b->position -= normal * depth * (aD ? massA / total : 1.0f);

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
    glm::vec3 d    = a->position - b->position;
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
    glm::vec3 local   = sphere->position - cube->position;
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
    float bottom = sphere->position.y - radius;
    if (bottom >= floorY) return;

    sphere->position.y = floorY + radius;

    PhysicalComponent* phys = sphere->physics;
    if (!phys) return;

    phys->isGrounded = true;
    if (phys->velocity.y < 0.0f)
        phys->velocity.y *= -phys->restitution;

    phys->velocity.x *= (1.0f - phys->friction);
    phys->velocity.z *= (1.0f - phys->friction);
}

void UPhysicsWorld::resolveCubeFloor(AActor* cube, const glm::vec3& half, float floorY)
{
    float bottom = cube->position.y - half.y;
    if (bottom >= floorY) return;

    cube->position.y = floorY + half.y;

    PhysicalComponent* phys = cube->physics;
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
    glm::vec3 diff = a->position - b->position;
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
