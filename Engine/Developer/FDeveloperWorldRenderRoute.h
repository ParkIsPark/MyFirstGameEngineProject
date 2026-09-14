#pragma once
#include "FDeveloperSettings.h"
// Editor-only production adapter. GameEngine never calls this factory.
std::unique_ptr<IDeveloperRenderRoute> CreateDeveloperWorldRenderRoute(ELegacyRendererOverride kind);
