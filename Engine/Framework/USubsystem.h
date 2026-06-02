#pragma once

// ---------------------------------------------------------------------------
// USubsystem — base for engine-level services (renderer, input, profiler, …)
// that the Engine owns and drives with a uniform Init/Tick/Shutdown lifecycle.
// GL-resource subsystems must do their GL work in Init() (after the context
// exists), never in the constructor.
// ---------------------------------------------------------------------------
class USubsystem
{
public:
    virtual ~USubsystem() = default;

    virtual void Init() {}
    virtual void Tick(float /*dt*/) {}
    virtual void Shutdown() {}
};
