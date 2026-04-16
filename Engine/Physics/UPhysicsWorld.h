#pragma once
#include <glm/glm.hpp>

class UScene;
class AActor;

class UPhysicsWorld
{
public:
    glm::vec3 gravity = glm::vec3(0.0f, -9.8f, 0.0f);

    void Tick(float dt, UScene& scene);

private:
    // --- floor (horizontal plane) ---
    void resolveSphereFloor (AActor* sphere, float radius,           float floorY);
    void resolveCubeFloor   (AActor* cube,   const glm::vec3& half,  float floorY);

    // --- AABB vs AABB ---
    // Handles static/dynamic internally; at least one must be dynamic.
    void resolveAABBvsAABB(AActor* a, const glm::vec3& halfA,
                           AActor* b, const glm::vec3& halfB);

    // --- Sphere vs AABB ---
    // Handles static/dynamic internally; at least one must be dynamic.
    void resolveSphereAABB(AActor* sphere, float radius,
                           AActor* cube,   const glm::vec3& half);

    // --- Sphere vs Sphere ---
    void resolveSphereSphere(AActor* a, float ra, AActor* b, float rb);

    // Helper: impulse-based velocity response along a collision normal.
    // normal points from b toward a.  Sets isGrounded if normal is upward.
    void applyImpulse(AActor* a, AActor* b,
                      const glm::vec3& normal, float depth, float restitution);
};
