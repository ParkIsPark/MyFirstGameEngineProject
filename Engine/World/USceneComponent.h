#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

class AActor;

// ---------------------------------------------------------------------------
// USceneComponent (P1) — a transform node in the scene graph.
//
// Holds a relative transform (Loc/Rot/Scale in DEGREES — matches the engine's
// GLM build, no GLM_FORCE_RADIANS) and an attachment parent. The world matrix
// is the parent chain composed top-down: world = parent.world * relative.
// The result is cached and recomputed lazily, only when a transform somewhere
// up the chain changes — a move marks this node and all descendants dirty
// (MarkDirty), and GetWorldMatrix() rebuilds the cache on the next read.
// ---------------------------------------------------------------------------
class USceneComponent
{
public:
    std::string name;

    glm::vec3 relLocation = glm::vec3(0.0f);
    glm::vec3 relRotation = glm::vec3(0.0f); // Euler XYZ, degrees
    glm::vec3 relScale    = glm::vec3(1.0f);

    USceneComponent*              attachParent = nullptr;
    std::vector<USceneComponent*> children;
    AActor*                       owner = nullptr;

    virtual ~USceneComponent();

    glm::mat4        GetRelativeMatrix() const;       // local T*R*S
    const glm::mat4& GetWorldMatrix() const;          // cached; parent.world * relative
    glm::vec3        GetWorldLocation() const;         // world-matrix translation

    void SetRelativeLocation(const glm::vec3& v) { relLocation = v; MarkDirty(); }
    void SetRelativeRotation(const glm::vec3& v) { relRotation = v; MarkDirty(); }
    void SetRelativeScale   (const glm::vec3& v) { relScale    = v; MarkDirty(); }

    void AttachTo(USceneComponent* parent);           // re-parent (updates child lists)
    void Detach();                                     // detach from parent

    void MarkDirty();                                  // invalidate self + all descendants

    // Test hook: total world-matrix recomputations (cache-correctness checks).
    static long s_recomputeCount;

private:
    mutable glm::mat4 cachedWorld_ = glm::mat4(1.0f);
    mutable bool      worldDirty_  = true;
};
