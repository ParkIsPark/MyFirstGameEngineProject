#pragma once
#include "UPrimitiveComponent.h"

// ---------------------------------------------------------------------------
// UShapeComponent — base for analytic-shape primitives (sphere/box/capsule).
// Adds no state of its own; it exists so collision/editor code can treat "a
// primitive that has an analytic shape" as one category, mirroring Unreal's
// UShapeComponent under UPrimitiveComponent.
// ---------------------------------------------------------------------------
class UShapeComponent : public UPrimitiveComponent
{
public:
    using UPrimitiveComponent::UPrimitiveComponent;
};
