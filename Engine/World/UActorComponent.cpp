#include "UActorComponent.h"

AActor* UActorComponent::GetOwner() const noexcept { return owner_; }
bool UActorComponent::IsEnabled() const noexcept { return enabled_; }
void UActorComponent::SetEnabled(bool enabled) noexcept { enabled_ = enabled; }
