#include "USceneComponent.h"
#include "FArchive.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

REGISTER_COMPONENT("Scene", USceneComponent)

long USceneComponent::s_recomputeCount = 0;

// Compose a T*R*S matrix. NOTE: this repo's GLM build takes rotation angles in
// DEGREES (no GLM_FORCE_RADIANS), so Euler degrees are passed directly. This is
// the same convention the old UMeshComponent::composeTRS used.
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

USceneComponent::~USceneComponent()
{
    // Unhook from the graph so no dangling pointers survive this node.
    if (attachParent)
    {
        auto& sib = attachParent->children;
        sib.erase(std::remove(sib.begin(), sib.end(), this), sib.end());
        attachParent = nullptr;
    }
    for (USceneComponent* c : children) c->attachParent = nullptr;
    children.clear();
}

glm::mat4 USceneComponent::GetRelativeMatrix() const
{
    return composeTRS(relLocation, relRotation, relScale);
}

const glm::mat4& USceneComponent::GetWorldMatrix() const
{
    if (worldDirty_)
    {
        cachedWorld_ = attachParent
            ? attachParent->GetWorldMatrix() * GetRelativeMatrix()
            : GetRelativeMatrix();
        worldDirty_ = false;
        ++s_recomputeCount;
    }
    return cachedWorld_;
}

glm::vec3 USceneComponent::GetWorldLocation() const
{
    return glm::vec3(GetWorldMatrix()[3]);
}

void USceneComponent::MarkDirty()
{
    worldDirty_ = true;
    for (USceneComponent* c : children) c->MarkDirty();
}

void USceneComponent::AttachTo(USceneComponent* parent)
{
    if (attachParent == parent) return;
    if (attachParent)
    {
        auto& sib = attachParent->children;
        sib.erase(std::remove(sib.begin(), sib.end(), this), sib.end());
    }
    attachParent = parent;
    if (parent) parent->children.push_back(this);
    MarkDirty();
}

void USceneComponent::Detach()
{
    AttachTo(nullptr);
}

void USceneComponent::Serialize(FArchive& ar)
{
    ar.Field("Name",  name);
    ar.Field("Loc",   relLocation);
    ar.Field("Rot",   relRotation);
    ar.Field("Scale", relScale);
    if (ar.IsLoading()) MarkDirty();
}
