#pragma once
#include "../Render/FRenderFeatures.h"
#include "../Core/FFeatureLifecycle.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <array>

using FDeveloperWarningSink = std::function<void(const std::string&)>;
void EmitDeveloperWarning(const FDeveloperWarningSink& sink, const std::string& message) noexcept;
struct FDeveloperSettings
{
    ELegacyRendererOverride legacyOverride = ELegacyRendererOverride::None;
    bool showDeprecatedFeatures = true;
    bool showExperimentalWarnings = true;
    static FDeveloperSettings Load(const std::filesystem::path& path = "Config/DeveloperSettings.ini",
                                   const FDeveloperWarningSink& warning = {}) noexcept;
    bool Save(const std::filesystem::path& path = "Config/DeveloperSettings.ini",
              const FDeveloperWarningSink& warning = {}) const noexcept;
};
const FFeatureDescriptor* LegacyFeature(ELegacyRendererOverride kind);
std::string DeveloperLifecycleWarning(const FFeatureDescriptor& feature);

class UWorld;
class ACamera;
class FRenderTarget;
struct FRenderQuality;
struct FBackendSelection;
// Borrowed frame inputs, used only for the duration of Render. No GL/UI dependency.
struct FDeveloperRenderFrame
{
    UWorld* world = nullptr;
    const ACamera* camera = nullptr;
    FRenderTarget* target = nullptr;
    const FRenderFeatures* features = nullptr;
    const FRenderQuality* quality = nullptr;
    const FBackendSelection* backend = nullptr;
    std::uint64_t contextGeneration = 0;
};
struct FDeveloperRouteStats
{
    std::uint64_t initializationAttempts = 0;
    std::uint64_t initializations = 0;
    std::uint64_t executions = 0;
    std::uint64_t shutdowns = 0;
};
class IDeveloperRenderRoute
{
public:
    virtual ~IDeveloperRenderRoute() = default;
    virtual ELegacyRendererOverride OverrideKind() const noexcept = 0;
    virtual bool Init() = 0;
    virtual void Shutdown() noexcept = 0;
    virtual bool Render(const FDeveloperRenderFrame& frame) = 0;
    virtual FDeveloperRouteStats Stats() const noexcept = 0;
};
class FDeveloperOverrideController
{
public:
    using Factory = std::function<std::unique_ptr<IDeveloperRenderRoute>(ELegacyRendererOverride)>;
    explicit FDeveloperOverrideController(Factory factory, FDeveloperWarningSink warning = {});
    ~FDeveloperOverrideController();
    bool Apply(ELegacyRendererOverride kind) noexcept;
    bool Render(const FDeveloperRenderFrame& frame, const std::function<bool()>& normal);
    void Shutdown() noexcept;
    ELegacyRendererOverride ActiveOverride() const { return active_; }
    FDeveloperRouteStats Stats(ELegacyRendererOverride kind) const noexcept;
private:
    Factory factory_;
    FDeveloperWarningSink warning_;
    std::unique_ptr<IDeveloperRenderRoute> route_;
    ELegacyRendererOverride active_ = ELegacyRendererOverride::None;
    std::array<bool, 2> warned_{{false, false}};
    std::array<FDeveloperRouteStats, 2> closedStats_{};
};
