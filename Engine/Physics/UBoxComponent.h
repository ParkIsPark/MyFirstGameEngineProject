#pragma once
#include "UShapeComponent.h"

// Analytic axis-aligned box collider + rigid body. Center = owner->position.
class UBoxComponent : public UShapeComponent
{
public:
    glm::vec3 halfExtents = glm::vec3(0.5f);

    using UShapeComponent::UShapeComponent;

    EShape GetShape() const override { return EShape::Box; }
    void   GetWorldAABB(glm::vec3& outMin, glm::vec3& outMax) const override;
};
