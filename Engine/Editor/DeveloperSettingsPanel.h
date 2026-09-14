#pragma once
#include "../Developer/FDeveloperSettings.h"
#include <vector>

struct FDeveloperLegacyChoice
{
    ELegacyRendererOverride overrideKind;
    std::string label;
    const FFeatureDescriptor* feature;
    std::string badge;
    std::string details;
};
struct FDeveloperSettingsDescription
{
    std::vector<FDeveloperLegacyChoice> legacyChoices;
    std::string activeWarning;
    std::string experimentalWarning;
};
// This view-model is implemented with the GL-free settings module.
FDeveloperSettingsDescription DescribeDeveloperSettings(
    const FDeveloperSettings& settings, const FRenderFeatures& features,
    ERayTracingBackend activeBackend);
bool DrawDeveloperSettingsPanel(bool& open, FDeveloperSettings& settings,
                               const FDeveloperSettingsDescription& description);
void DrawDeveloperLifecycleBadges(const FDeveloperSettingsDescription& description);
