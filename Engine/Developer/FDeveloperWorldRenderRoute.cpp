#include "FDeveloperWorldRenderRoute.h"
#include "../Render/FDeprecatedWorldRenderExecutor.h"

namespace {
class FDeveloperWorldRenderRoute final : public IDeveloperRenderRoute
{
public:
    explicit FDeveloperWorldRenderRoute(std::unique_ptr<IDeprecatedWorldRenderExecutor> executor)
        : kind_(executor->OverrideKind()), executor_(executor.get()), renderer_(std::move(executor)) {}
    ELegacyRendererOverride OverrideKind() const noexcept override { return kind_; }
    bool Init() override
    {
        renderer_.Init();
        if (executor_->Ready()) return true;
        renderer_.Shutdown();
        return false;
    }
    FDeveloperRouteStats Stats() const noexcept override
    {
        const auto& stats=executor_->LifecycleStats();
        return {stats.initializationAttempts,stats.initializations,stats.executions,stats.shutdowns};
    }
    void Shutdown() noexcept override { renderer_.Shutdown(); }
    bool Render(const FDeveloperRenderFrame& frame) override
    {
        if (!frame.world || !frame.camera || !frame.target || !frame.features || !frame.quality || !frame.backend) return false;
        return renderer_.Render(*frame.world,*frame.camera,*frame.target,*frame.features,
                                *frame.quality,*frame.backend,frame.contextGeneration);
    }
private:
    const ELegacyRendererOverride kind_;
    // Borrowed from renderer_, whose executor ownership outlives this pointer.
    const IDeprecatedWorldRenderExecutor* executor_;
    UWorldRenderer renderer_;
};
}
std::unique_ptr<IDeveloperRenderRoute> CreateDeveloperWorldRenderRoute(ELegacyRendererOverride kind)
{
    auto executor=CreateDeprecatedWorldRenderExecutor(kind);
    if (!executor) return nullptr;
    return std::make_unique<FDeveloperWorldRenderRoute>(std::move(executor));
}
