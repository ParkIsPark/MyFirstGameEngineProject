#include "UMeshComponent.h"
#include "UMesh.h"
#include "AActor.h"
#include "URay.h"

#include <glm/gtc/matrix_transform.hpp>

// Compose a T*R*S matrix. NOTE: this repo's GLM build takes rotation angles in
// DEGREES (no GLM_FORCE_RADIANS defined), so Euler degrees are passed directly.
static glm::mat4 composeTRS(const glm::vec3& t, const glm::vec3& rDeg, const glm::vec3& s)
{
    glm::mat4 m(1.0f);
    m = glm::translate(m, t);
    m = glm::rotate(m, rDeg.z, glm::vec3(0.0f, 0.0f, 1.0f));
    m = glm::rotate(m, rDeg.y, glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::rotate(m, rDeg.x, glm::vec3(1.0f, 0.0f, 0.0f));
    m = glm::scale(m, s);
    return m;
}

glm::mat4 UMeshComponent::GetWorldMatrix(const AActor& owner) const
{
    const glm::mat4 actorM = composeTRS(owner.position, owner.rotation, owner.scale);
    const glm::mat4 relM   = composeTRS(relPosition,    relRotation,    relScale);
    return actorM * relM;
}

bool UMeshComponent::intersect(const URay& worldRay, const AActor& owner,
                               float& outT, int& outTri, float& outU, float& outV) const
{
    if (!mesh) return false;
    return mesh->intersect(worldRay, GetWorldMatrix(owner), outT, outTri, outU, outV);
}
