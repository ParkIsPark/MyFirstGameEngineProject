#include "UActorComponent.h"
#include "FArchive.h"

AActor* UActorComponent::GetOwner() const noexcept { return owner_; }
bool UActorComponent::IsEnabled() const noexcept { return enabled_; }
void UActorComponent::SetEnabled(bool enabled) noexcept { enabled_ = enabled; }

void UActorComponent::Serialize(FArchive& ar)
{
    bool enabled = enabled_;
    ar.Field("Enabled", enabled);
    if (ar.IsLoading()) enabled_ = enabled;
}
