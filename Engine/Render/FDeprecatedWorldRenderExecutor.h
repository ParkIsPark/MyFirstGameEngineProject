#pragma once

#include "FRenderFeatures.h"
#include "UWorldRenderer.h"

#include <memory>

// Developer-only seam for the retained educational whole-frame renderers.
// Normal Editor/Game rendering never includes or calls this factory.
std::unique_ptr<IWorldRenderExecutor> CreateDeprecatedWorldRenderExecutor(
    ELegacyRendererOverride overrideKind);
