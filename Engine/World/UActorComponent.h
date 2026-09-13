#pragma once
#include <string_view>

class AActor;
class FArchive;

// Non-spatial component state. Only actor adoption may change ownership.
class UActorComponent
{
public:
    virtual ~UActorComponent() = default;
    virtual std::string_view TypeName() const = 0;
    virtual void Serialize(FArchive& ar);
    virtual void BeginPlay() {}
    virtual void Tick(float) {}
    virtual void EndPlay() {}
    // Runtime resources may need End even after the serialized toggle is off.
    virtual bool RequiresEndPlay() const noexcept { return false; }

    AActor* GetOwner() const noexcept;
    bool IsEnabled() const noexcept;
    void SetEnabled(bool enabled) noexcept;

protected:
    UActorComponent() = default;
    // Copies describe a fresh component; assignment keeps the destination owner.
    UActorComponent(const UActorComponent& other) : enabled_(other.enabled_) {}
    UActorComponent& operator=(const UActorComponent& other)
    {
        enabled_ = other.enabled_;
        return *this;
    }
    // Preserve the existing physics constructors' owner hint until adoption.
    explicit UActorComponent(AActor* owner) : owner_(owner) {}

private:
    friend class AActor;
    AActor* owner_ = nullptr;
    bool enabled_ = true;
    bool nativeTickFailed_ = false;
};
