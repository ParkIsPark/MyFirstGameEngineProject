#include "USphereComponent.h"
#include "../World/AActor.h"

void USphereComponent::GetWorldAABB(glm::vec3& outMin, glm::vec3& outMax) const
{
    const glm::vec3 c = owner ? owner->GetActorLocation() : glm::vec3(0.0f);
    outMin = c - glm::vec3(radius);
    outMax = c + glm::vec3(radius);
}
