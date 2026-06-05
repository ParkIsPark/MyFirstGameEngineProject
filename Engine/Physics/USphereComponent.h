#pragma once
#include "UShapeComponent.h"

// Analytic sphere collider + rigid body. Center = owner->position.
class USphereComponent : public UShapeComponent
{
public:
    float radius = 0.5f;

    using UShapeComponent::UShapeComponent;

    EShape GetShape() const override { return EShape::Sphere; }
    void   GetWorldAABB(glm::vec3& outMin, glm::vec3& outMax) const override;
};
