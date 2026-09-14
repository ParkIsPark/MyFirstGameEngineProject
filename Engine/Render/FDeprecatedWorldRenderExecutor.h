#pragma once

#include "FRenderFeatures.h"
#include "UWorldRenderer.h"

#include <cstdint>
#include <memory>

struct FDeprecatedWorldRenderExecutorStats
{
    std::uint64_t initializations = 0;
    std::uint64_t executions = 0;
    std::uint64_t shutdowns = 0;
};

class IDeprecatedWorldRenderExecutor : public IWorldRenderExecutor
{
public:
    virtual ELegacyRendererOverride OverrideKind() const noexcept = 0;
    virtual const FDeprecatedWorldRenderExecutorStats& LifecycleStats()
        const noexcept = 0;
};

// Developer-only seam for the retained educational whole-frame renderers.
// Normal Editor/Game rendering never includes or calls this factory.
std::unique_ptr<IDeprecatedWorldRenderExecutor> CreateDeprecatedWorldRenderExecutor(
    ELegacyRendererOverride overrideKind);
