#include "UBoxComponent.h"
#include "../World/AActor.h"

void UBoxComponent::GetWorldAABB(glm::vec3& outMin, glm::vec3& outMax) const
{
    const glm::vec3 c = owner ? owner->position : glm::vec3(0.0f);
    outMin = c - halfExtents;
    outMax = c + halfExtents;
}
